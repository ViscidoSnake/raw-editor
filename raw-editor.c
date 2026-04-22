/*** includes ***/

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

void die(const char *s) {
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
  
  // atexit fondamentale per ripristinare lo stato iniziale del terminale, in pratica esegue le funzioni registrate come in uno stack LIFO una volta che il main termina (anche con exit che forza l'interruzione immediata del main)
  atexit(disableRawMode);

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

/*** init ***/

int main(){
  enableRawMode();

  int n_timeout = 0;
  int n_writing = 0;

  while (1) {
    // valore di default di c è il terminatore di stringa che corrisponde al byte 0 ed è un carattere non printabile
    char c = '\0';

    long int n = read(STDIN_FILENO, &c, 1);

    if(n==0) 
      n_timeout++; 
    if(n==1)
      n_writing++;
    if(n==-1)
      // in pratica in questo caso la read può assumere 3 valori diversi: 0 1 e -1, quest'ultimo segnala un errore e quindi il programma va bloccato
      die("read");
    
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
    if (c == 'q') {
      printf("tempo totale di digitazione: %f s\r\n", ((float)(n_writing)*10)/1000);
      printf("tempo totale timeout: %f s\r\n", ((float)(n_timeout)*10)/1000);
      break;
    }
  }
  return 0;
}
