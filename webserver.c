#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>

#define NUM_THREAD (4)

int cl_sk[NUM_THREAD];

/* semafori classici */
sem_t main_sem; //gestisce il thread main che a sua volta gestisce gli altri thread
sem_t thread_sem[NUM_THREAD];

int occupato[NUM_THREAD]; //tiene conto dei thread occupati
int tot_occupati = 1; //tiene conto del thread main

FILE *log_file;

char* NOME_SERVER="webserver";
char* NOME_SERVER_MIO ="neoben(FreeBSD)";

char* ERR_404="<html><head><title></title></head><body><p>404 Not Found</p></body></html>";
char* ERR_501="<html><head><title></title></head><body><p>501 Not Implemented</p></body></html>";

char* HOST;
uint16_t PORTA;
char* CARTELLA_BASE;

/* struttura richieste che arrivano al server */
struct richiesta 
{
	char* metodo;
	char* path;
	char* protocollo;
};

/* struttura riposte del server */
struct risposta
{
	char* tipo; 
 	char* intestazione;
	char* body;
	int lunghezza; //numero di byte del corpo della risposta 
};

/**********************************************************
***		      RIC_ESTENSIONE			***
***    ricava l'estensione del file che gli viene       ***
***   passato per argomento (sotto forma di stringa)    ***
**********************************************************/
char* ric_estensione(char* str) //ricava l'estensione dei file e la restituisce come stringa
{
    int i;
    int cont = 0;
    char* estensione;
    for(i = strlen(str); str[i]!='.'; i--)
    {
      cont++;
    }
    return estensione = &str[strlen(str)-cont+1];
}	

/**********************************************************
***		      GET_FILESIZE			***
***    ricava la dimensione del file che gli viene      ***
***   passato per argomento (sotto forma di stringa)    ***
**********************************************************/
int get_filesize( char* filename )
{
    struct stat file_info;
    if( !stat( filename, &file_info ) )
      return file_info.st_size; //restituisce la dimensione del file
    return -1; //restituisce -1 in caso di errore
}

/**********************************************************
***		 INTERPRETAZIONE_RICHIESTA		***
***    interpreta la richiesta che giunge al server:    ***
***    -controlla che sia una richiesta di tipo GET     ***
*** -controlla che il protocollo sia HTTP/1.0 o HTTP/1.1***
*** -ricava automaticamente il file index.html nel caso ***
***in cui nn sia esplicitamente inserito nella richiesta***
***     -effettua il controllo sui MIME supportati      ***
**********************************************************/
int interpretazione_richiesta(struct richiesta *ric,struct risposta *risp,char *buffer)
{
	char str[7];
	char* tipo_file;

	/* la richiesta arriva compatta quindi e' necessario scinderla -> funzione strtok */
	ric->metodo = strtok(buffer," ");
	ric->path = strtok(NULL," ");
	ric->protocollo = strtok(NULL,"\r");

	if(strcmp(ric->metodo,"GET"))
	  return 0; //metodo non supportato
	if(strcmp(ric->protocollo,"HTTP/1.0") && strcmp(ric->protocollo,"HTTP/1.1"))
	  return 0; //protocollo non riconosciiuto
	
	/* se e' presente solo la radice, riconosce automaticamente la index */
	if(ric->path[strlen(ric->path)-1] == '/')
	  strcat(ric->path,"index.html");

	tipo_file = ric_estensione(ric->path);
      
	/* analisi dei MIME supportati */
	if(!strcmp(tipo_file,"html") || !strcmp(tipo_file,"htm") || !strcmp(tipo_file,"css") || !strcmp(tipo_file,"js") || !strcmp(tipo_file,"txt"))
	{
	  strcpy(str,"text/");
	  strcat(str,tipo_file);
	  risp->tipo = malloc(strlen(str)+1); //allocazione in memoria del tipo di file supportato
	  strcpy(risp->tipo,str);
	  return 1;
	}
	if(!strcmp(tipo_file,"gif") ||!strcmp(tipo_file,"jpg") || !strcmp(tipo_file,"jpeg") || !strcmp(tipo_file,"png"))
	{
	  strcpy(str,"image/");
	  strcat(str,tipo_file);
	  risp->tipo = malloc(strlen(str)+1); //allocazione in memoria del tipo di file supportato
	  strcpy(risp->tipo,str);
	  return 1;
	}
	return 0;
	/* 
	se restituisce 0 siamo nei possibili casi di:
	   - metodo non supportato
	   - protocollo non riconosciuto
	   - MIME non supportato
	*/
}

/**********************************************************
***		           CORPO			***
***      funzione richiamata automaticamente alla       ***
***              creazione di ogni thread:              ***
***     -riceve le richieste provenienti sui soket      ***
***   -costruisce e invia le risposte alle richieste    ***
***                provenienti sui soket                ***
***     -gestisce il multi-thread tramite semafori      ***
**********************************************************/
void* corpo(void* num)
{
	struct richiesta r;
	struct risposta s;
	char *completo; //pacchetto completo da inviare: head + corpo
	char buffer[1024];
	char cartella_file[100];
	char control[50];
	int controllo_richiesta;
	int len;
	int ret;
	int dim;
	int lun;
	FILE *c; //mi serve per aprire il file relativo alla CARTELLA_BASE

	printf("%s: [Thread #%d] In attesa di richieste\n",NOME_SERVER,(int)num);
	fprintf(log_file,"%s: [Thread #%d] In attesa di richieste\n",NOME_SERVER,(int)num);
	while(1)
	{
	    log_file = fopen("./log","a"); //apertura del file log in modalita' append
	    
	    sem_wait(&thread_sem[(int)num]);
	    tot_occupati++;

	    len = recv(cl_sk[(int)num],(void*)buffer,1024,0);

	    if(strstr(buffer,"\r\n\r\n") == NULL) //attesa secondo invio telnet
	    {
		lun = recv(cl_sk[(int)num],(void*)control,50,0);
		while(lun > 2)
		lun = recv(cl_sk[(int)num],(void*)control,50,0);
	    }
	    
	    controllo_richiesta = interpretazione_richiesta(&r,&s,buffer);
	    printf("%s: [Tread #%d] Ricevuta richiesta (soket %d): %s\n",NOME_SERVER,(int)num,cl_sk[(int)num],r.path);
	    fprintf(log_file,"%s: [Tread #%d] Ricevuta richiesta (soket %d): %s\n",NOME_SERVER,(int)num,cl_sk[(int)num],r.path);
	    /* 
	    controllo_richiesta == 0 siamo nei possibili casi di:
	      - metodo non supportato
	      - protocollo non riconosciuto
	      - MIME non supportato
	    */
	    if(controllo_richiesta == 0)
	    {
	      printf("%s: [Tread #%d] Richiesta non valida (soket %d)\n",NOME_SERVER,(int)num,cl_sk[(int)num]);
	      fprintf(log_file,"%s: [Tread #%d] Richiesta non valida (soket %d)\n",NOME_SERVER,(int)num,cl_sk[(int)num]);
	      s.intestazione = malloc(50);
	      sprintf(s.intestazione,"HTTP/1.0 501 Not Implemented\n\n"); 
	      s.body = malloc(strlen(ERR_501));
	      strcpy(s.body,ERR_501);
	      dim = strlen(s.intestazione) + strlen(ERR_501);
	      completo = malloc(dim);
	      strcpy(completo,s.intestazione);
	      strcat(completo,s.body);
	      free(s.body);
	      free(s.intestazione);
	      len = send(cl_sk[(int)num],(void*)completo,dim,0);
	      free(completo);
	    }
	    else
	    {
	      strcpy(cartella_file,CARTELLA_BASE);
	      strcat(cartella_file,r.path);
	      c = fopen(cartella_file,"r"); //apertura file in modalita' lettura
	      if(c == NULL) // file vuoto quindi file inesistente
	      {
		printf("%s: [Tread #%d] File inesistente (soket %d): %s\n",NOME_SERVER,(int)num,cl_sk[(int)num],r.path);
		fprintf(log_file,"%s: [Tread #%d] File inesistente (soket %d): %s\n",NOME_SERVER,(int)num,cl_sk[(int)num],r.path);
		s.intestazione = malloc(50);
		sprintf(s.intestazione,"HTTP/1.0 404 Not Found\n\n"); 
		s.body = malloc(strlen(ERR_404));
		strcpy(s.body,ERR_404);
		dim = strlen(s.intestazione) + strlen(ERR_404);
		completo = malloc(dim);
		strcpy(completo,s.intestazione);
		strcat(completo,s.body);
		free(s.body);
		free(s.intestazione);
		len = send(cl_sk[(int)num],(void*)completo,dim,0);
		free(completo);
	      }
	      else //tutto ok -> inviare la risposta HTTP  in assenza di errori
	      {
		s.intestazione  = malloc(100);
		s.lunghezza = get_filesize(cartella_file);
		sprintf(s.intestazione,"HTTP/1.0 200 OK\nContent-Type: %s\nContent-Length: %d\nServer: %s\n\n",s.tipo,s.lunghezza,NOME_SERVER_MIO);
		free(s.tipo);
		s.body = malloc(s.lunghezza);
		ret = fread(s.body,1,s.lunghezza,c);
		fclose(c); //chiusura file
		dim = s.lunghezza + strlen(s.intestazione); //dimensione di tutta la risposta: head + corpo 
		completo = malloc(dim);
		strcpy(completo,s.intestazione);
		/* N.B.: il corpo e' stato allocato in memoria */
		memcpy(&completo[strlen(s.intestazione)],s.body,s.lunghezza);
		/* ho copiato il body in pacchetto a partire dall'intestazione */
		/* ora in pacchetto ho tutti i dati da inviare: head + body */
		free(s.body);
		free(s.intestazione);
		len = send(cl_sk[(int)num],(void*)completo,dim,0);
		if(len == -1)
		{
		  printf("%s: Errore nell'effettuazione della Send\n",NOME_SERVER);
		  fprintf(log_file,"%s: Errore nell'effettuazione della Send\n",NOME_SERVER);
		}
		printf("%s: [THREAD #%d]: Risposta inviata (socket %d): %s\n",NOME_SERVER,(int)num,cl_sk[(int)num],r.path);
		fprintf(log_file,"%s: [THREAD #%d]: Risposta inviata (socket %d): %s\n",NOME_SERVER,(int)num,cl_sk[(int)num],r.path);
		free(completo);
	      }
	      
	    }
	    ret = close(cl_sk[(int)num]); //chiusura connessione soket
	    fclose(log_file); //chiusura file di log
	    sem_post(&main_sem);
	    occupato[(int)num] = 0;
	    tot_occupati--;
	} //fine while(1) 
}

/**********************************************************
***		            MAIN			***
**********************************************************/
int main(int argc, char *argv[])
{
	int sk; 
	int ret;
	int len;
	int i;
	const int BACKLOG_DIM = 10; //dimensione massima per la coda degli elementi pendenti
	const int on = 1; //valore standard -> lo uso nella setsockopt
	
	pthread_t threads[NUM_THREAD];
	sem_init(&main_sem,0,(NUM_THREAD-1)); //inizializzazione semaforo thread main

	log_file = fopen("./log","a"); //apertura del file log in modalita' append
	
	struct sockaddr_in my_addr; 
	struct sockaddr_in cl_addr; //strutture dati per indirizzi

	if(argc!=4)
	{
	    printf("ERRORE: sintassi del comanado errata\n");
	    printf("Scrivere il comando nella forma: $./webserver <host> <porta> <cartella_base>\n");
	    exit(1);
	}
	
	HOST=argv[1];
	PORTA=atoi(argv[2]);
	CARTELLA_BASE=argv[3];

	printf("%s: Porta: %d\n",NOME_SERVER,PORTA);
	fprintf(log_file,"%s: Porta: %d\n",NOME_SERVER,PORTA);
	printf("%s: Indirizzo: %s\n",NOME_SERVER,HOST);
	fprintf(log_file,"%s: Indirizzo: %s\n",NOME_SERVER,HOST);

	/* creazione socket */
	sk = socket(PF_INET,SOCK_STREAM,0);
	/* restituisce 0 in caso di successo e -1 in caso di errore */
	if(sk == -1)
	{
	    printf("%s: Errore nella creazione del socket\n",NOME_SERVER);
	    fprintf(log_file,"%s: Errore nella creazione del socket\n",NOME_SERVER);
	    exit(0);
	}
	
	/* Riavvio il server in caso di connessioni sulla porta */
	ret = setsockopt(sk,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
	/* restituisce 0 in caso di successo e -1 in caso di errore */
	if(ret == -1)
	{
	    printf("%s: Impossibile settare l'opzione SO_REUSEADDR\n",NOME_SERVER);
	    fprintf(log_file,"%s: Impossibile settare l'opzione SO_REUSEADDR\n",NOME_SERVER);
	    exit(0);
	}
	
	printf("%s: Descrittore del socket in ascolto: %d\n",NOME_SERVER,sk);
	fprintf(log_file,"%s: Descrittore del socket in ascolto: %d\n",NOME_SERVER,sk);

	/* Assegnamento di IP e Porta al soket */
	memset(&my_addr,0,sizeof(my_addr)); //azzera la struttura
	my_addr.sin_family = AF_INET; //IPv4 address
	my_addr.sin_port = htons(PORTA); //network ordered
	inet_pton(AF_INET,HOST,&my_addr.sin_addr.s_addr);

	ret = bind(sk,(struct sockaddr*) &my_addr,sizeof(my_addr));
	/* restituisce 0 in caso di successo e -1 in caso di errore */
	if(ret == -1)
	{
	    printf("%s: Errore nell'effetuazione della Bind\n",NOME_SERVER);
	    fprintf(log_file,"%s: Errore nell'effetuazione della Bind\n",NOME_SERVER);
	    exit(0);
	}
	
	printf("%s: Bind effettuata con successo\n",NOME_SERVER);
	fprintf(log_file,"%s: Bind effettuata con successo\n",NOME_SERVER);
  
	ret = listen(sk,BACKLOG_DIM);
	/* restituisce 0 in caso di successo e -1 in caso di errore */
	if(ret == -1)
	{
	    printf("%s: Errore nell'effetuazione della Listen\n",NOME_SERVER);
	    fprintf(log_file,"%s: Errore nell'effetuazione della Listen\n",NOME_SERVER);
	    exit(0);
	}	
	printf("%s: Server in ascolto con successo sul soket\n",NOME_SERVER);
	fprintf(log_file,"%s: Server in ascolto con successo sul soket\n",NOME_SERVER);

	for( i=0 ; i<NUM_THREAD; i++)
	{
	  sem_init(&thread_sem[i],0,0); //innizializzazione dei semafori
	  ret = pthread_create(&threads[i],NULL,corpo,(void*)i); //creazione dei THREAD
	  occupato[i] = 0; //inizialmente nessun thread è occupato
	}

	fclose(log_file); //chiusura del file log

	len = sizeof(cl_addr);
	i = 0;
	while(1)
	{
	    log_file = fopen("./log","a"); //apertura del file log in modalita' append
	    cl_sk[i] = accept(sk,(struct sockaddr*)&cl_addr,(unsigned int*)&len);
	    /* restituisce 0 in caso di successo e -1 in caso di errore */
	    if(cl_sk[i] == -1)
	    {
		printf("%s: Errore nell'effetuazione dell' Accept\n",NOME_SERVER);
		fprintf(log_file,"%s: Errore nell'effetuazione dell' Accept\n",NOME_SERVER);
		exit(0);
	    }
	    printf("%s: Connessione con il client (socket %d)\n",NOME_SERVER,cl_sk[i]);
	    fprintf(log_file,"%s: Connessione con il client (socket %d)\n",NOME_SERVER,cl_sk[i]);
	    printf("%s: Thread %d disponibile (socket %d), occupati %d su %d\n",NOME_SERVER,i,cl_sk[i],tot_occupati,NUM_THREAD);
	    fprintf(log_file,"%s: Thread %d disponibile (socket %d), occupati %d su %d\n",NOME_SERVER,i,cl_sk[i],tot_occupati,NUM_THREAD);
	    sem_post(&thread_sem[i]); //sveglia un thread
	    sem_wait(&main_sem); //quando ci sono NUM_THREAD attivi blocca il thread main
	    occupato[i] = 1;
	    while(1)
	    {
	      if(occupato[i] == 0) //controllo se il thread e' occupato/libero
		break;
	      /* se il thread e'occupato bisogna aggiornare l'indice -> prendere il thread successivo */
	      i = (i+1) % NUM_THREAD; //aggiorna l'indice
	    }
	    fclose(log_file); //chiusura del file log
	}
}

/**********************************************************
***		       FINE WEBSERVER			***
**********************************************************/

