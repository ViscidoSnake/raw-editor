/*** includes ***/

// questi 3 define servono per far funzionare la getline()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <stdarg.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define RAW_EDITOR_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define RAW_EDITOR_QUIT_TIMES 2

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  DEL_KEY,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY
};

/*** data ***/
// usato per tenere in memoria il testo scritto nell'editor
typedef struct erow {
  int size;
  char *chars;
  int rsize;
  char *render; // buffer usato per effettuare il render cioè in pratica quello effettivamente stampato
} erow;

struct editorConfig {
  int cx, cy; // posizioni x e y del cursore 
  int rx;
  int rowoff; // righe di offset, importante per implementazione dello scrolling
  int coloff; // bordo orizzontale di offset, importante per implementare scrolling orizzontale 
  int screenrows; // larghezza della finestra in cui l'editor è eseguito espressa in caratteri
  int screencols; // ampiezza della finestra in cui l'editor è eseguito espressa in caratteri
  int numrows; // numero di righe nel file aperto dall'editor
  erow *row;  // importante, in pratica definiamo un array di oggetti erow dove la lunghezza di questo array sarebbe quella scritta in numrows
  char *filename;
  char statusmsg[150];
  int dirty;
  time_t statusmsg_time;
  int mod;
  int render;
  struct termios orig_termios;
};
struct editorConfig E;

/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);

/*** terminal ***/

void die(const char *s) {
  // sequnze di escape per pulire il terminale e riposizionare il cursore in alto a sinistra
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  
  // perror guarda le variabili errno che sono usate per indicare eventuali errori durante l'esecuzione e le stampa, tipicamente descrivono già bene il problema ma è possibile anche precedere il messaggio di errore da una stringa
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

  // fondamentale per ripristinare le condizioni iniziali del terminale!
  atexit(disableRawMode);
  
  struct termios raw = E.orig_termios;
  // ISIG disabilita i segnali SIGINT e SIGTSTP inviati da Ctrl-C e Ctrl-Z, si evitano in pratica freez dell'output e arresto forzato del programma
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  // IXON bit per disabilitare i seganli emessi da Ctrl-S e Ctrl-Q che praticamente fermano o riprendono il flusso dati in arrivo verso il terminale, anche questi scomodi in questo particolare caso. ICRNL disabilita carriage return e line feed, in pratica questo permette di associare al byte 13 la cobinazione Ctrl-M e anche Invio, prima erano associati al byte 10
  raw.c_iflag &= ~(ICRNL | IXON);
  // OPOST è un bit che permette di processare sempre come \n\r il carattere \n, in altre parole il line feed (\n) scritto nel codice poi nell'output viene sempre mostrato come carrige return + line feed, questo compartamento è quasi più legato alla storia dell'informatica e in questo contesto (reaizzare un editor) è interessante poterli usare separatamente. Dopo questa modifica si osserva come nel printf è importante scrivere \r\n
  raw.c_oflag &= ~(OPOST);
  // molto interessanti questi ultimi due indici in pratica VMIN specifica il minimo numero di byte che la read deve leggere prima di fare return mentre la seconda stabilisce un tempo massimo oltre il quale la read fa return. Allora attenzione la read fa return, in questo caso, ogni volta che legge anche un solo byte, impostare VMIN a 0 rende il ciclo while effettivamente iterativo cioè non ci si ferma alla read, impostarlo a 1 rende la read "bloccante" mentre valori maggiori non hanno senso perche come detto il retun avviene dopo ogni byte letto. Impostare VTIME a 1 impone un return forzato della read ogni 100 millisecondi, o meglio fa in modo tale che la read resti in attesa di byte (in questo caso 1) per 100 ms e dopo tale tempo ritorna 0 cioè i byte letti in quel lasso di tempo (che possono essere 0 o 1 in questo caso)
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

// su laptop HOME_KEY e END_KEY sono f11 e f12 rispettivamente, potrebbero esserci però anche altri pulsanti o combinazioni che non conosco e danno le stesse combinazioni che danno questi pulsanti, per questo sono presenti in vari case
int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  // controllo fondamentale, in pratica mi assicuro di processare solo caratteri ASCII code escludendo sostanzialmente sutto ciò che è UTF-8, per fare il conrollo faccio AND bitwise del byte letto e mi assicuro sempre che il bit più significativo sia 0 in quanto i caratteri ASCII vanno da 0 (00000000) a 127 (01111111) 
  if ((c & 0x80) != 0) {
    return 128;
  }
  
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  } else {
    // printf("  %d  ", c);
    return c;
  }

}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations ***/

// questa funziona calcola dinamicamente la posizione che deve avere il cursore nell'interfaccia di editor, è fondamentale perchè caratteri come il tab (\t) vengono renderizzati come sequenza di più spazzi!
int editorRowCxToRx(erow *row, int cx) {
  
  // da rifare
  
  // int rx = 0;
  // int j = 0;
  // for (j = 0; j < cx; j++) {
  //   if (row->chars[j] == '\t')
  //     rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
  //   rx++;
  // }
  // return rx;



  // int rx = 0;
  // int j = 0;
  // while(j < cx){
  //   if (row->chars[j] != '\\') {
  //     rx++;
  //     j++;
  //   }
  //   else{
  //     j++;
  //     switch (row->chars[j])
  //     {
  //     case 'S':
  //       rx+=4 + 1;
  //       j+=1;
  //       break;
      
  //     case 'F':
  //     case 'B':
  //       rx+=19 + 1;
  //       j+=12;

  //     break;
      
  //     default:
  //         j++;
  //         rx++;
  //       break;
  //     }
  //   }
    
  // }
  // return rx;



  // while(j < cx){
  //   if (row->chars[j] != '\\') {
  //     rx++;
  //     j++;
  //   } else {
  //     switch (row->chars[j+1]) {
  //       case 's':
  //         rx+=1;
  //         j+=2;

  //         break;
    
  //       case 'f':
  //       case 'b':
  //         // rx+=8;
  //         rx+=3;
  //         j+=15;
          
  //       break;
        
  //       default:
  //         j++;
  //         break;
  //     }
  //   }
  // }

  // return rx;



  int rx = 0;
  int j = 0;

  while (j < cx) {

      if (row->chars[j] != '\\') {

          rx++;
          j++;
          continue;
      }

      if (j + 1 >= row->size) {
          rx++;
          j++;
          continue;
      }

      switch (row->chars[j + 1]) {

          case 'S':
              j += 2;
              break;

          case 'F':
          case 'B':
              j += 13;
              break;

          default:
              rx++;
              j++;
              break;
      }
  }

  return rx;
}


// questa funzione è quella che definisce come renderizzare in output deterinate parti del testo
// gestione del carattere tab (\t), questo carattere è problematico perche se non trattato viene gestito dal terminale che lo renderizza solitamente usando una regola interna ad esso cioè segue i tab stop che sono dati solitamente come multipli interi di 4 (ma non sempre) quindi il tab sposta il cursore sempre su queste colonne, tuttavia questo render automatico è problematico per l'editor perchè il carattere tab (1 carattere) viene espanso di un numero arbritrario, non noto a priori (esempio tab stop 8: ca\tne, nel trminale il testo è renderizzato su 10 colonne di cui 6 spazzi (ha senso, il tab sulla terza clonna viene espanso in 5 spazzi per raggiungere la colonna 8 dove viene scritto n e poi e) MA l'editor ne vede solo 6 in quanto non ha espanso il tab in spazzi e lo ha contato con carattere normale).   
void editorUpdateRow(erow *row) {
  
  int special = 0;
  int term = 0;

  int j;
  for (j = 0; j < row->size; j++) {
    if ((row->chars[j] == '\\') && ((row->chars[j+1] == 'F')||(row->chars[j+1] == 'B'))) special++;
    else if ((row->chars[j] == '\\') && (row->chars[j+1] == 'S')) term++; 
  }
  free(row->render);
  
  row->render = malloc(row->size - special*13 - term*2 + special*19 + term*4 + 1 );

  
  j=0;
  int rindx=0;
  while(j < row->size){
    if (row->chars[j] != '\\') {
      row->render[rindx] = row->chars[j];
      rindx++;
      j++;
    }
    else{
      j++;
      switch (row->chars[j])
      {
      case 'S':
        memcpy(&(row->render[rindx]),"\x1b[0m",4);
        rindx+=4;
        j+=1;
        break;
      
      case 'F':
      case 'B':
        if(row->chars[j] == 'F') memcpy(&(row->render[rindx]),"\x1b[38;2;",7);
        else memcpy(&(row->render[rindx]),"\x1b[48;2;",7);
        rindx+=7;
        j+=1;
        memcpy(&(row->render[rindx]), &(row->chars[j]), 11);
        rindx+=11;
        j+=11;
        row->render[rindx] = 'm';
        rindx+=1;
      break;
      
      default:
          row->render[rindx] = row->chars[j];
          j++;
          rindx++;
        break;
      }
    }
    
  }
  
  row->render[rindx] = '\0';
  row->rsize = rindx;
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // rialloca memoria per l'array di erow che si espande di 1 perchè viene letta una nuova riga
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1); // puntatore ad una zona della memoria dove scriverò i caratteri che costituiscono la riga letta, il +1 è per aggiungere poi il carattere terminatore di stringa
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);
  E.numrows++;  // incremento finale essendo stata aggiunta la riga appena processata 
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

// notare che la funzione aggiunge un solo byte quindi caratteri multi byte non vengono inseriti
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar() {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;
  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}


/*** file i/o ***/
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}
// carica con dati erow presente nella struttura E con il nome di row, in questa versione i dati caricati sono la prima riga di un file passato come argomento nel momento in cui viene lanciato l'editor. in particolare la getline legge la prima riga di questo file (tutto ciò che è scritto prima del carattere new line \n compreso) e il while successivo serve per scartare il carattere \n e \r che praticamente non vorranno essere stampati.
void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);
  
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);;
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL) return;
  int len;
  char *buf = editorRowsToString(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        editorSetStatusMessage("%d bytes written to disk", len);
        E.dirty = 0;
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** append buffer ***/
// si tratta di una struttura dati che praticamente è una lista (non proprio), in C serve implementarla usando puntatori e chiamate per allocazione dinamica della memoria, la sintassi usata per la scrittura è abbastanza avanzata, devo ripredere un po questa parte. Nel progetto questa struttura dati serve per scrivere con una sola write un set di caratteri quindi volendo un elenco di stringhe 

struct abuf {
  char *b;
  int len;
};
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** input ***/
void editorMoveCursor(int key) {
  // in pratica row contiene l'indirizzo della riga in cui si trova il cursore nel momento in cui viene premuta una delle freccette, E.cy potrebbe essere maggiore di E.numrows perchè è previsto lo scroll verticale oltre l'ultima riga del file letto dall'editor, in tal caso è restituito un puntatore NULL in quanto effettivamente non ce nessun elemento erow da puntare
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) { // implementa il fatto che premendo arrow left quando si è sul primo carattere di una riga allora il curore viene posizionato sull'ultimo caarttere della riga precedente 
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) { // con questo controllo evito che si possa verificare uno scroll orizzontale che vada oltre l'ultimo carattere presente nella riga (per essere precisi, il cursore si posiziona in corrispondenza del carattere terminatore che nel terminale è renderizzato come uno spazio essendo non stampabile)
        E.cx++;
      } else if (row && E.cx == row->size) { // implementa il fatto che premendo arrow right quando il cursore è alla fine del carattere della riga corrente questo viene posizionato all'inizio del carattere della riga successiva
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) { // evita che il cursore sia posizionato in coordinata y negativa
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) { // evita che il cursore assuma valori maggiori rispetto a E.numrows ovvero il numero complessivo di righe lette nel file dall'aeditor 
        E.cy++;
      }
      break;
  }

  // ----
  // questo blocchetto evita che spostandosi verticalmente comunque il limite dello scroll orizzontale (cursore che al massimo arriva fino all'ultimo carattere della riga) sia sempre rispettato e in particolare se E.cx eccede allora viene riportato al valore massiomo dato da row->size cioè la dimensione in caratteri dell'attuale riga
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
  // ----
}

void editorProcessKeypress() {
  static int quit_times = RAW_EDITOR_QUIT_TIMES; // variabile statica, fondamentale che lo sia altrimenti sarebbe impossibile tenere in memoria il numero di volte in cui è stato premuto Ctrl + q
  
  int c = editorReadKey();

  switch (c) {

    case 128:
      editorSetStatusMessage("WARNING!!! Il carattere digitato non è ASCII code quindi non può essere inserito!");
      break;
    
    case '\r': // estrema attenzione, ricordati che ottieni questo valore (13 decimale) premendo sulla tastiera la combinazione Ctrl - m !!! (quindi non puoi usarla)
      editorInsertNewline();
      break;
    // CTRL_KEY è una macro che applica una maschera (operazione AND) bit a bit, la maschera è di 8 bit e sono i seguenti 00011111 (in decimale 31), in questo caso tale maschera viene applicata al carattere q corrispondente al byte 01110001 (113 in decimale), il risultato è il seguente byte 00010001 (17 in decimale) dato come la combinazione di Ctrl-q. In sostanza la chiave è che la macro è ben fatta perchè permette di rimappare tutte le lettere dell'afabeto ma combinate a Ctrl, chiaramente questo è possibile anche al modo in cui è stato costruito ASCII
    case CTRL_KEY('q'):
      // sequnze di escape per pulire il terminale e riposizionare il cursore in alto a sinistra
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
        "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    

    case CTRL_KEY('k'):
      E.mod = 0;
      break;

    case CTRL_KEY('r'):
      E.render = !E.render;
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }

        // Interessante, in base che sia premuto pageup o pagedown è previsto un ciclo che esegue times volte arrow up o arrow down ovvero quindi il cursore viene fatto scorrere nella coordinata y di uno in alto o in basso 
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

      case CTRL_KEY('s'):
        editorSave();
        break;
    
    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
    
    case CTRL_KEY('l'):
    case '\x1b':
      break;


    default:
      if (c < 32) { // allora, questo serve per evitare di inserire nell'editor caratteri non printabili. Infatti tutte le combinazioni Ctrl + (lettera) imporrebbero la scrittura di un caarttere non printabile (quindi byte che però non hanno corrispondenza ad un simbolo), tutte le combinazioni tollerate infatti precedono questo if e NON implicano la scrittura dei byte nel file modificato dall'editor.
        editorSetStatusMessage("WARNING!!! Il carattere digitato non è stampabile!");
        break; 
      }
      editorInsertChar(c);

  }

  quit_times = RAW_EDITOR_QUIT_TIMES;
}

/*** output ***/

// funzione che implementa lo scroll verticale, in pratica aggiorna rowoff in funzione dei valori assunti da E.cy che cambiano in base al numero di volte che si premono freccia su o freccia giù. idem per lo scroll orizzontale ma qui la variabile aggiornata è coloff e la variabile considerata è E.cx
void editorScroll() {

  // E.rx = 0;
  // if (E.cy < E.numrows) {
  //   E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  //   if(E.render == 1){
  //     // E.r2x = editorRowRxToR2x(&E.row[E.cy], E.rx);
  //     E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

  //   } else {
  //     E.rx = E.cx;
  //   }
  // }

  E.rx = E.cx;


  if (E.cy < E.rowoff) {     // implementa praticamente lo scroll verticale verso l'alto 
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {  // implementa praticamente lo scroll verticale ma verso il basso
    E.rowoff = E.cy - E.screenrows + 1;
  }
   if (E.rx < E.coloff) { // implementa lo scroll orizzontale verso sinistra
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) { // implementa lo scroll orizzontale a destra, il +1 è perchè viene scrollato un carattere alla volta
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) { // scrive il messaggio di benvenuto solo se non viene passato nessun file in input  
        //----
        // interessante questo blocco perchè usa la scanf per scrivere dentro un buffer una serie di caratteri, il buffer a sua volta viene poi caricato nella lista di strighe da scrivere
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "raw-editor -- version %s", RAW_EDITOR_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        // per centrare testo
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        
        abAppend(ab, welcome, welcomelen);
        //----
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      // len contiene la lunghezza della riga da scrivere ed è data come la lunghezza effettiva della stringa meno il numero di caratteri offset (ricorda che offset è sempre positivo ed è applicato a destra dello schermo)
      int len = E.row[filerow].size - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].chars[E.coloff], len);
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80]; // varibili usate per scrivere nome del file aperto e numero di riga in cui si trova il cursore
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
  E.filename ? E.filename : "[No Name]", E.numrows,
    E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorByteReader() {
  
  int w = E.screencols;
  int h = E.screenrows;

  int menurow  = 5;
  int toprowp = (h/2)-(menurow/2);
  int bottomrowp = (h/2)+(menurow/2);

  struct abuf menu = ABUF_INIT;

  abAppend(&menu, "\x1b[?25l", 6); // nasconde il cursore nel terminale
  abAppend(&menu, "\x1b[48;2;255;0;0m", 15); // applica sfondo rosso ai caratteri scritti da questo momento in poi

  // definisco posizioni in cui si dovrà riposizionare il cursore per ridisegnare alcune parti del menu
  int xpstdinp;
  int ypstdinp;
  char xypstdinp[20];
  int lenxypstdinp;

  int xpstdout;
  int ypstdout;
  char xypstdout[20];
  int lenxypstdout;

  // disegno tutto il menu
  for(int i=toprowp; i<=bottomrowp; i++){

    int rightspaces = 0;
    char cp[30];
    int cplen = sprintf(cp, "\x1b[%d;1H", i);
    abAppend(&menu, cp, cplen);

    if(i == toprowp){ // prima riga del menu
      // 9 sono i byte che servono per scrivere la stringa Byte mode
      rightspaces = w - (w/2 - 9/2 + 9);
      int c = w/2 - 9/2;
      while(c){
        abAppend(&menu, " ", 1);
        c--;
      }
      abAppend(&menu, "Byte mode", 9);
    }
    if(i == toprowp + 1){ // terza riga del menu
      // 35 sono i byte che servono per scrivere la stringa "Byte: "
      rightspaces = w - 35;
      ypstdinp = i;
      if(rightspaces - 15 < 0) { // allora il controllo serve per capire se la finestra che renderizza è abbasanza grande da mostrare la stringa statica più il risultato dei byte letti dalla standard input, 15 sarebbe una stima larga dei caratteri necessari, un caso limite del tipo xxx xxx xxx xxx. se non ce questo spazio ovvero entro dentro if allora la posizione del cursore sarà sempre posta a 1 quindi il risultato dei byte letti sovrascriverà la stringa statica, decremento rightspaces perchè poi questo viene riusato alla fine per fare il fill della riga con spazi e potrei essere entrato nell'if con rightspaces > 0
        xpstdinp = 0;
        rightspaces-=15;
      }
      else xpstdinp = 35 + 1;
      
      lenxypstdinp = sprintf(xypstdinp,"\x1b[%d;%dH",ypstdinp,xpstdinp);
      abAppend(&menu, "Byte scritti sulla standard input: ", 35);
    }
    if(i == toprowp + 2){ // quarta riga del menu
      // 50 sono i byte che servono per scrivere Risultato dei....
      rightspaces = w - 50;
      ypstdout = i;
      if(rightspaces - 2 < 0) {
        xpstdout = 0;
        rightspaces-=2;
      }
      else xpstdout = 50 + 1;
      
      lenxypstdout = sprintf(xypstdout,"\x1b[%d;%dH",ypstdout,xpstdout);
      abAppend(&menu, "Risultato dei byte scritti sulla standard output: ", 50);
    }
    if(i == toprowp + 3){ // quinta riga del menu
      // 69 sono i byte che servono per scrivere la stringa help
      rightspaces = w;
    }
    if(i == toprowp + 4){ // quinta riga del menu
      // 69 sono i byte che servono per scrivere la stringa help
      rightspaces = w - 69;
      abAppend(&menu, "Scrivere sulla standard input il byte 113 (carattere 'q') per uscire.", 69);
    }
    
    if(rightspaces<0) rightspaces = 0; // si verifica se le stringhe statiche sono più lunghe di w, in questo caso verranno troncate e non serviranno spazzi a destra

    // scrive rightspaces caratteri spazio
    while(rightspaces){
      abAppend(&menu, " ", 1);
      rightspaces--;
    }


  }
  
  write(STDOUT_FILENO, menu.b, menu.len);
  abFree(&menu); //posso liberare già adesso la memoria perchè no lo userò più, infatti una volta che è stato scritto nella standard output non è più previsto di ridisegnarlo


  while (1) {
    char readbuf[32]; // buffer di lettura sulla standard input, lette al max sequenze di 32 byte, ampiamente sufficiente, anzi direi eccessivo
    int nread = read(STDIN_FILENO, readbuf, sizeof(readbuf)); // leggo oggni 100 ms la standard input e metto il numero di byte letti in nread
    if (nread == -1 && errno != EAGAIN) die("read");

    // printf("%d", nread);

    if (nread != 0) {

      if ((readbuf[0] == 'q') && (nread == 1)) break; //appena leggo carattere q esco 

      //---gestione lettura dei byte sulla standard input---
      char *outrawstdinp = malloc(w - xpstdinp + 1);
      memset(outrawstdinp, ' ', w - xpstdinp + 1); // metodo rapido per scrivere su tutta la memoria allocata il carattere spazio
      char *outrawstdout = malloc(w - xpstdout + 1);
      memset(outrawstdout, ' ', w - xpstdout + 1); // metodo rapido per scrivere su tutta la memoria allocata il carattere spazio
      

      int nc = 0; //indice per spostarsi dentro outrawstdinp, serve perchè non è noto a priori quanti byte saranno scritti nella stringa di output! tipo il numero 12 occuoerà 2 byte ma 123 3 byte
      int i=0;
      while(i<nread){
        int n = sprintf(&outrawstdinp[nc], "%d ", (unsigned char)readbuf[i]);
        nc = nc + n;
        i++;
      }
      
      // esiste ora un problema: potrebbero essere digitate sequenze ASCII escape ovvero che iniziano con il byte 27 (ad esempio è comune che le tastiere stampino tali sequenze per tasti come le frecce) oppure anche sequenze di byte non stampabili (praticamente i caratteri che vanno da 0 a 31 e il byte 127), le due tipologie di sequenze causano problemi se vengono stampate direttamente perchè vengono interpretate dal terminale che potrebbe spostare il cursore, cambiare stati interni di esso ecc, per questo conviene non stamparle ma sostituire i byte con una stringa di avviso. il carattere spazio, 32, non è molto visibile però viene stampato normalmente (cioè viene gestito como un byte comune)
      if(((char unsigned)readbuf[0] <= 31) || ((char unsigned)readbuf[0] == 127)) {
        memcpy(outrawstdout, "non stampabile!", 15);
      }else{
        // readbuf non contiene sequenze escape o caratteri non printabili, procedo a ricopiarlo semplicemente. NOTA: i caratteri verrano interpretati dal terminale in base al tipo di codifica adottata dal terminale stesso, comunemente i terminali odierni hanno di dafault la codifica UTF-8 che è capace di interpretare varie sequenze di byte
        memcpy(outrawstdout, readbuf, nread); 
      }
      
      write(STDOUT_FILENO, xypstdinp, lenxypstdinp); // riposiziono ogni volta il cursore all'inizio della stringa "Byte... "
      write(STDOUT_FILENO, outrawstdinp, w - xpstdinp + 1);
      free(outrawstdinp);

      write(STDOUT_FILENO, xypstdout, lenxypstdout);  // riposiziono ogni volta il cursore all'inizio della stringa "Risultato... "
      write(STDOUT_FILENO, outrawstdout, w - xpstdout + 1);
      free(outrawstdout);

    }
  }
  
  write(STDOUT_FILENO, "\x1b[0m", 4); //ripristino colori originali terminale
  write(STDOUT_FILENO, "\x1b[?25h", 6); //cursore nuovamente visibile
  E.mod = 1;
}


void editorDrawRenderRows(struct abuf *ab) {
  int y;

  for (y = 0; y < E.screenrows; y++) {
    
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) continue;
    int len = E.row[filerow].rsize - E.coloff;
    if (len < 0) len = 0;
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, &E.row[filerow].render[E.coloff], len);
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }

}


// in questa funzione vengono usati caratteri escape supportati dall'emulatore di terminale, le sequenze VT100 sono quelle più comunemente supportate dai "recenti" emulatori, per fare in modo che l'editor sia compatibile con ancora più terminali fino quasi a definirsi indipendente da essi è necessario fare riferimento a terminfo oppure anche alla libreria ncurses. Spunti molto interessanti per modellare l'editor in modo che risulti il più compatibile possibile.
void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6); // nasconde il cursore nel terminale
  abAppend(&ab, "\x1b[H", 3); // riposiziona il cursore in alto a sinistra dello schermo (coordinate 1;1)


  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  // ----
  // muove il cursore, cioè lo posiziona in relazione agli input dati
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1); 

  abAppend(&ab, buf, strlen(buf));
  // ----

  // abAppend(&ab, "\x1b[H", 3);
  abAppend(&ab, "\x1b[?25h", 6); // rende nuovamente visibile il cursore

  write(STDOUT_FILENO, ab.b, ab.len);
  
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  E.statusmsg_time = time(NULL);
  
  char* st = ctime(&(E.statusmsg_time)); //restituisce la data in un formato leggibile, attenzione viene sempre messo, prima del terminatore di stringa, il carattere \n (new line)
  int strl = strlen(st); //ricorda che strlen NON considera nel conto il carattere terminatore!!!
  sprintf(E.statusmsg, "[%s", st);
  sprintf(&(E.statusmsg[strl]), "] ");
  
  va_list ap;
  va_start(ap, fmt);
  vsprintf(&(E.statusmsg[strl+2]), fmt, ap);
  va_end(ap);
}


void editorRefreshRenderScreen(){
  editorScroll();

  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6); // nasconde il cursore nel terminale
  abAppend(&ab, "\x1b[H", 3); // riposiziona il cursore in alto a sinistra dello schermo (coordinate 1;1)
  abAppend(&ab, "\x1b[92m", 5);

  editorDrawRenderRows(&ab);
  // editorDrawStatusBar(&ab);
  // editorDrawMessageBar(&ab);

  // ----
  // muove il cursore, cioè lo posiziona in relazione agli input dati
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1); 

  abAppend(&ab, buf, strlen(buf));
  // ----

  abAppend(&ab, "\x1b[0m", 4);
  abAppend(&ab, "\x1b[?25h", 6); // rende nuovamente visibile il cursore

  write(STDOUT_FILENO, ab.b, ab.len);
  
  abFree(&ab);

}


/*** init ***/

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.dirty = 0;
  E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.mod = 1;
  E.render = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");

  E.screenrows -= 2; // tolgo una screenrow perche la riservo per la status bar e una per scrivere il messaggio
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  
  // serve per verificare la presenza di un argomento dato durante lancio del programma
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");
  // osserva che questo while "non cicla" come ci si aspetterebbe infatti: dopo la chiamata a editorProcessKeypress questa chiama una sola volta la editorReadKey la quale ha un ciclo while dove l'esecuzione effettiva resta bloccata fin tanto che non viene premuto un pulsante (o meglio, fin tanto che non viene scritto un byte nella standard input) oppure si verifica errore di lettura (per essere precisi il ciclo while in questione è scandito dal ritmo con cui la read fa return che in questo caso è 100 ms), se viene scritto un byte la editorReadKey fa return (finalmente) e il codice riprende da dentro editorProcessKeypress che ad ora ha solo uno switch case, quando termina anche questa funzione si ritorna al main, in particolare dentro il while "infinito" che esegue quanto è scritto dopo la editorProcessKeypress per poi ricominciare dalla editorRefreshScreen.
  while (1) {
    if(!E.render) editorRefreshScreen();
    else editorRefreshRenderScreen();
    if(E.mod) editorProcessKeypress();
    else editorByteReader();
  }

  return 0;
}
