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
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define RAW_EDITOR_VERSION "0.0.1"
#define KILO_TAB_STOP 8

enum editorKey {
  ARROW_LEFT,
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
  struct termios orig_termios;
};
struct editorConfig E;

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
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}

// questa funzione è quella che definisce come renderizzare in output deterinate parti del testo
// gestione del carattere tab (\t), questo carattere è problematico perche se non trattato viene gestito dal terminale che lo renderizza solitamente usando una regola interna ad esso cioè segue i tab stop che sono dati solitamente come multipli interi di 4 (ma non sempre) quindi il tab sposta il cursore sempre su queste colonne, tuttavia questo render automatico è problematico per l'editor perchè il carattere tab (1 carattere) viene espanso di un numero arbritrario, non noto a priori (esempio tab stop 8: ca\tne, nel trminale il testo è renderizzato su 10 colonne di cui 6 spazzi (ha senso, il tab sulla terza clonna viene espanso in 5 spazzi per raggiungere la colonna 8 dove viene scritto n e poi e) MA l'editor ne vede solo 6 in quanto non ha espanso il tab in spazzi e lo ha contato con carattere normale).   
void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}


void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // rialloca memoria per l'array di erow che si espande di 1 perchè viene letta una nuova riga 

  int at = E.numrows; // numero di righe correnti, attenzione se ce ne sono ad esempio 10 allora indici di E.row vanno da 0 a 9, essendo stata effettuata la realloc ora però E.row ha 11 posizioni che vanno quindi da 0 a 10, per questo nelle righe successive viene usato direttamente E.numrows

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1); // puntatore ad una zona della memoria dove scriverò i caratteri che costituiscono la riga letta, il +1 è per aggiungere poi il carattere terminatore di stringa
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++; // incremento finale essendo stata aggiunta la riga appena processata 
}


/*** file i/o ***/

// carica con dati erow presente nella struttura E con il nome di row, in questa versione i dati caricati sono la prima riga di un file passato come argomento nel momento in cui viene lanciato l'editor. in particolare la getline legge la prima riga di questo file (tutto ciò che è scritto prima del carattere new line \n compreso) e il while successivo serve per scartare il carattere \n e \r che praticamente non vorranno essere stampati.
void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
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
  int c = editorReadKey();

  switch (c) {
    // CTRL_KEY è una macro che applica una maschera (operazione AND) bit a bit, la maschera è di 8 bit e sono i seguenti 00011111 (in decimale 31), in questo caso tale maschera viene applicata al carattere q corrispondente al byte 01110001 (113 in decimale), il risultato è il seguente byte 00010001 (17 in decimale) dato come la combinazione di Ctrl-q. In sostanza la chiave è che la macro è ben fatta perchè permette di rimappare tutte le lettere dell'afabeto ma combinate a Ctrl, chiaramente questo è possibile anche al modo in cui è stato costruito ASCII
    case CTRL_KEY('q'):
      // sequnze di escape per pulire il terminale e riposizionare il cursore in alto a sinistra
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    
    case DEL_KEY:
      break;


    case PAGE_UP:
    case PAGE_DOWN:
      {
        // Interessante, in base che sia premuto pageup o pagedown è previsto un ciclo che esegue times volte arrow up o arrow down ovvero quindi il cursore viene fatto scorrere nella coordinata y di uno in alto o in basso 
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
    
    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
      E.cx = E.screencols - 1;
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
    
    case 110:
      printf("%c",c);
      break;
  }
}


/*** output ***/

// funzione che implementa lo scroll verticale, in pratica aggiorna rowoff in funzione dei valori assunti da E.cy che cambiano in base al numero di volte che si premono freccia su o freccia giù. idem per lo scroll orizzontale ma qui la variabile aggiornata è coloff e la variabile considerata è E.cx
void editorScroll() {

  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

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
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

// in questa funzione vengono usati caratteri escape supportati dall'emulatore di terminale, le sequenze VT100 sono quelle più comunemente supportate dai "recenti" emulatori, per fare in modo che l'editor sia compatibile con ancora più terminali fino quasi a definirsi indipendente da essi è necessario fare riferimento a terminfo oppure anche alla libreria ncurses. Spunti molto interessanti per modellare l'editor in modo che risulti il più compatibile possibile.
void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6); // nasconde il cursore nel terminale
  abAppend(&ab, "\x1b[H", 3); // riposiziona il cursore in alto a sinistra dello schermo (coordinate 1;1)


  editorDrawRows(&ab);

  // ----
  // muove il cursore, cioè lo posiziona in relazione agli input dati
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1); 

  abAppend(&ab, buf, strlen(buf));
  // ----

  // abAppend(&ab, "\x1b[H", 3);
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
  E.row = NULL;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  
  // serve per verificare la presenza di un 
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  // osserva che questo while "non cicla" come ci si aspetterebbe infatti: dopo la chiamata a editorProcessKeypress questa chiama una sola volta la editorReadKey la quale ha un ciclo while dove l'esecuzione effettiva resta bloccata fin tanto che non viene premuto un pulsante (o meglio, fin tanto che non viene scritto un byte nella standard input) oppure si verifica errore di lettura (per essere precisi il ciclo while in questione è scandito dal ritmo con cui la read fa return che in questo caso è 100 ms), se viene scritto un byte la editorReadKey fa return (finalmente) e il codice riprende da dentro editorProcessKeypress che ad ora ha solo uno switch case, quando termina anche questa funzione si ritorna al main, in particolare dentro il while "infinito" che esegue quanto è scritto dopo la editorProcessKeypress per poi ricominciare dalla editorRefreshScreen.
  while (1) {
    editorRefreshScreen(); 
    editorProcessKeypress();
  }

  return 0;
}
