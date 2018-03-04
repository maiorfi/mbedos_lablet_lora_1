# mbedOS LoRa Lablet

Sample per test request/reply (simmetrico) LoRa MAC con Semtech SX1272/SX1276 (#define all'inizio del main.cpp e relativa (alternativa) esclusione su .mbedignore, es. "SX1276Lib_RTOS/*" per usare il modulo SX1272).

Per la compilazione occorre abilitare il il supporto a C++11 modificando i file di profilo di compilazione contenuti in /mbed-os/tools/profiles/. Per GCC ad esempio va modificata l'opzione "-std=gnu++98" in "-std=gnu++11".
 
## Test END-2-END (uart-host to uart-host)

> (un host può essere implementato con un PC con un dongle USB/Seriale e un'applicazione terminal come ad esempio RealTerm o TeraTerm)

> (assumendo che HOST<N> sia l'HOST con LORA ADDRESS N, dove l'indirizzo è gestito a livello applicativo, visto che a livello MAC non è definito)

#### Test invio comando (senza ack) in broadcast a tutta la rete LORA

HOST1 invia (tramite uart) __"!C|0|303#"__ (Comando a indirizzo 0, ergo broadcast, con payload 303) -> a tutti gli host della rete lora deve arrivare __"^C|1|303@"__ (il secondo item, 1, rappresenta l'indirizzo lora mittente del comando)

#### Test invio query (con ack) ad uno specifico nodo LORA

HOST1 invia (tramite uart) __"!Q|2|202#"__ (Comando a indirizzo 2 con payload 202) -> ad HOST2 deve arrivare __"^Q|1|202@"__ (il secondo item, 1, rappresenta l'indirizzo lora mittente del comando) -> HOST2 invia (tramite uart) __"!R||0#"__ (ultimo item >=0 significa ack positivo) oppure __"!R||-1#"__ (ultimo item <0 significa ack negativo) -> ad HOST1 deve arrivare __"^R|2|0@"__ (se è stato inviato un ack positivo) o __"^R|2|65535@"__ (in caso di invio di ack negativo). __NOTA:__ I payload degli "ack" vengono inviati non alterati, ma dal lato dell'host sono considerati interi con segno, mentre dal lato del nodo lora sono interi senza segno a 16 bit.

## Test LORA-2-HOST

> premendo il pulsante blu viene inviato un messaggio su rete lora ad un indirizzo che "ruota" tra 0 (broadcast) e 4 (definito da un #define nel main.cpp) escludendo il proprio indirizzo. Il payload del messaggio è un contatore. Per tutti i messaggi non broadcast (ergo con indirizzo di destinazione diverso da 0) è atteso un ack (reply con payload con bit 15 a 0) o un nack (reply con payload con bit 15 a 1) 

## Test automatico (con RealTerm)

> vanno aperte due istanze di RealTerm configurate per aprire ciascuna una connessione host-uart (default 115200 8-N-1) e vanno poi lanciati gli script "serial_test_script_nodo_1.txt" e "serial_test_script_nodo_2.txt" rispettivamente sugli host con indirizzo 1 e 2, con ritardo di riga a 1000ms e avviamento 1->2 entro 100-200 ms 
