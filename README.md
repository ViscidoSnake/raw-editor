# raw-editor
Questo progetto consiste nella realizzazione di un editor di testo scritto in C e operante nell'emulatore shell usato dal sistema operativo che esegue l'editor. L'idea e la base dello sviluppo è presa da un progetto già esistente ovvero il progetto kilo sviluppato da [Salvatore Sanfilippo](https://github.com/antirez), la mia idea è di espanderlo ad esempio introducendo funzionalità come l'inserimento di immagini (trasformate in ASCII art), il copia e incolla e varie altre sempre usando librerie standard del C e API POSIX.

Per la base dello sviluppo sto seguendo un tutorial che costruisce passo passo l'editor e permette di comprenderlo in tutte le sue parti, di seguito il [link](https://viewsourcecode.org/snaptoken/kilo/01.setup.html).

## Compilazione ed Esecuzione
Come compilatore viene usato gcc v11.4.0 e per automatizzare il processo di compilazione è usato CMake 4.3, lo sviluppo è effettuato su macchina Linux. Per compilare il progetto basta eseguire CMake da dentro la directory, quindi eseguire il comando `make`, per lanciare l'editor invece `./raw-editor`.

## Funzionalità aggiuntive
In questa parte vengono raccolte e spiegate le funzionalità aggiunte dell'editor rispetto al modello di partenza. Tutte le funzionalità aggiuntive sono inserite cercando di rispettare la filosofia con cui è stato creato il progetto kilo ovvero solo librerie operanti a basso livello.

Allo stato attuale non sono presenti funzionalità aggiutive rispetto a quelle già implementate da kilo in quanto l'attenzione è focalizzata sulla comprensione completa del codice che è stato già scritto per implementare le funzioni base.
