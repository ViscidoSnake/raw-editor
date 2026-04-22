/*** includes ***/

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

struct termios orig_termios;

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
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");  
  
  struct termios raw = orig_termios;
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

char editorReadKey() {
  int nread;
  char c;
  // molta attenzione, si resta nel ciclo while e qundi nella funzione fin tanto che la read non legge un byte dalla standard input, appena ne viene letto uno oppure si verifica errore la funzione fa return  
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

/*** input ***/

void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
    // CTRL_KEY è una macro che applica una maschera (operazione AND) bit a bit, la maschera è di 8 bit e sono i seguenti 00011111 (in decimale 31), in questo caso tale maschera viene applicata al carattere q corrispondente al byte 01110001 (113 in decimale), il risultato è il seguente byte 00010001 (17 in decimale) dato come la combinazione di Ctrl-q. In sostanza la chiave è che la macro è ben fatta perchè permette di rimappare tutte le lettere dell'afabeto ma combinate a Ctrl, chiaramente questo è possibile anche al modo in cui è stato costruito ASCII
    case CTRL_KEY('q'):
      // sequnze di escape per pulire il terminale e riposizionare il cursore in alto a sinistra
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
  }
}

/*** output ***/

// in questa funzione vengono usati caratteri escape supportati dall'emulatore di terminale, le sequenze VT100 sono quelle più comunemente supportate dai "recenti" emulatori, per fare in modo che l'editor sia compatibile con ancora più terminali fino quasi a definirsi indipendente da essi è necessario fare riferimento a terminfo oppure anche alla libreria ncurses. Spunti molto interessanti per modellare l'editor in modo che risulti il più compatibile possibile.
void editorRefreshScreen() {
  write(STDOUT_FILENO, "\x1b[2J", 4); // scrive sulla standard input sequenza escape che fa clear di tutto il terminale e riporta il cursore in basso a sinistra del terminale (diciamo in basso a sinistra della finestra che esegue il terminale)
  write(STDOUT_FILENO, "\x1b[H", 3); // ancora una sequenza di escape, fatta questa volta da 3 byte, il comando H riposiziona il cursore considerando il termianle come una matrice di caratteri, le coordinate di riposizionamento sono espresse nell'argomento, come si vede qui l'argomento è vuoto quindi si usa il default che è 1;1 (colonna 1 riga 1, la numerazione parte da 1 non da 0)

}

/*** init ***/

int main(){

  // atexit fondamentale per ripristinare lo stato iniziale del terminale, in pratica esegue le funzioni registrate come in uno stack LIFO una volta che il main termina (anche con exit che forza l'interruzione immediata del main)
  atexit(disableRawMode);
  
  enableRawMode();

  // osserva che questo while "non cicla" come ci si aspetterebbe infatti: dopo la chiamata a editorProcessKeypress questa chiama una sola volta la editorReadKey la quale ha un ciclo while dove l'esecuzione effettiva resta bloccata fin tanto che non viene premuto un pulsante (o meglio, fin tanto che non viene scritto un byte nella standard input) oppure si verifica errore di lettura (per essere precisi il ciclo while in questione è scandito dal ritmo con cui la read fa return che in questo caso è 100 ms), se viene scritto un byte la editorReadKey fa return (finalmente) e il codice riprende da dentro editorProcessKeypress che ad ora ha solo uno switch case, quando termina anche questa funzione si ritorna al main, in particolare dentro il while "infinito" che esegue quanto è scritto dopo la editorProcessKeypress per poi ricominciare dalla editorRefreshScreen.
  while (1) {
    editorRefreshScreen(); 
    editorProcessKeypress();
  }

  return 0;
}
