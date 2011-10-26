#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ccn/ccn.h>
#include <ccn/ccnd.h>
#include <ccn/uri.h>

#include <pthread.h>

#define SRV_IP "127.0.0.1"
#define SRV_PORT 1240
#define BUFLEN 1324
#define NPACK 10

char *progname;
unsigned char *media_buffer;
pthread_mutex_t lock;

static void mylog(char *msg)
{
    printf("[%s]: %s\n", progname, msg);
}

char *get_interest_name(struct ccn_upcall_info *info)
{
	struct ccn_charbuf *c;
	struct ccn_indexbuf *comps;
	
	c = ccn_charbuf_create();
	comps = info->interest_comps;
	printf("Interest components: %d\n", info->interest_comps->n);
	//ccn_uri_append (c, info->interest_ccnb, comps, 1);

	//printf("%s\n", ccn_charbuf_as_string(c));
    return ccn_charbuf_as_string(c);
}

enum ccn_upcall_res incoming_interest(struct ccn_closure *selfp,
                                      enum ccn_upcall_kind kind,
                                      struct ccn_upcall_info *info)
{
    int res = 0, i;
    struct ccn_charbuf *cob;
	struct ccn_charbuf *temp, *pname, *name;
    enum ccn_content_type content_type = CCN_CONTENT_DATA;
    struct ccn_signing_params sp = CCN_SIGNING_PARAMS_INIT;
	unsigned char *buf;
	
	sp.type = content_type;
	
	temp = ccn_charbuf_create();
	pname = ccn_charbuf_create();
	name = ccn_charbuf_create();
	
	/*Get URI from parameter*/
	/*Changed info->interest_comps to info->interest_combs->n -- change if it doesnt work --*/
    res = ccn_uri_append(pname, info->interest_ccnb, (int)info->interest_comps, 1);
	ccn_name_from_uri(name, ccn_charbuf_as_string(pname));
	
	/*Demo message
	buf = malloc(sizeof(char)*40);
	strcpy(buf, "Demo Message");*/
	
    if (res < 0)
    {
        fprintf(stderr, "%s: bad ccn URI: %s\n", progname, ccn_charbuf_as_string(name));
        exit(1);
    }
	
	printf("Interest name: %s\n", ccn_charbuf_as_string(pname));
	
	temp->length = 0;
	

	
	printf("Attempting to sign...\n");
	
	pthread_mutex_lock(&lock);  
	printf("Packet content: %s\n", media_buffer);
	/*Signing should be done here*/
	res = ccn_sign_content(info->h, temp, name, &sp, media_buffer, BUFLEN);    
   // printf("Buffer: %s, size: %d\n", ccn_charbuf_as_string(temp), temp->length);
	
	pthread_mutex_unlock(&lock);  
	
    switch (kind)
    {
    case CCN_UPCALL_FINAL:
        mylog("CCN_UPCALL_FINAL");
        break;
    case CCN_UPCALL_INTEREST:
	  mylog("CCN_UPCALL_INTEREST");
        if (ccn_content_matches_interest(temp->buf, temp->length,
                                         1, NULL,
                                         info->interest_ccnb, info->pi->offset[CCN_PI_E],
                                         info->pi))
        {
            mylog("Incoming interest");
            res = ccn_put(info->h, temp->buf, temp->length);
			
            if(res >= 0)
            {
                mylog("Successful");
                selfp->intdata = 1;
                //ccn_set_run_timeout(info->h, 0);
                return(CCN_UPCALL_RESULT_INTEREST_CONSUMED);
            }
            else
            {

                fprintf(stderr, "ccn_put failed (res == %d)\n", res);
                exit(1);

            }

        }
        break;
    default:
        mylog("default");
        break;
    }

    return(CCN_UPCALL_RESULT_OK);
}


void *receive_stream(void *threadid)
{

    struct sockaddr_in si_me, si_other;
    int s, ret, slen = sizeof(si_other);
	unsigned long packet_count = 0;
    char buf[BUFLEN];
	size_t bytes_total = 0, bytes_read;

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
    {
        mylog("socket error");
        exit(-1);
    }


    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(SRV_PORT);

    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = bind(s, &si_me, sizeof(si_me));

    if (ret < 0)
    {
        mylog("bind() failed\n");
        exit(1);
    }


	
    while(recvfrom(s, buf, BUFLEN, 0, &si_other, &slen) != -1)
    {

        /*Deploy current packet within global buffer*/
		pthread_mutex_lock(&lock);   //Mutex -> important do not remove
		memcpy(media_buffer, buf, BUFLEN);
		pthread_mutex_unlock(&lock); 
		packet_count++;
		bytes_total += BUFLEN;
		

		if(packet_count % 100 == 0)
		{
			printf("%ld packets received\n", packet_count);
			printf("%d total bytes\n", bytes_total);
		}
		
    }


	pthread_exit(NULL);
    close(s);
    return 0;
}

int main(int argc, char **argv)
{

	/*Thread declaration*/
	pthread_t stream_thread;
	int rc;
	long t = 0;

	/*complex mutex stuff*/
	pthread_mutex_init(&lock, NULL);
	media_buffer = malloc(BUFLEN);
	
	progname = argv[0];
    if(argc < 2)
    {
        perror("You must supply a URI");
        return -1;
    }
    struct ccn *ccn = NULL;
    struct ccn_charbuf *name = NULL;
    struct ccn_closure in_interest = {.p=&incoming_interest};
    struct ccn_charbuf *pname = NULL;
    struct ccn_charbuf *temp = NULL;
	struct ccn_charbuf *locbuffer = NULL;
    int res;
    unsigned char *buf = NULL;
    enum ccn_content_type content_type = CCN_CONTENT_DATA;
    struct ccn_signing_params sp = CCN_SIGNING_PARAMS_INIT;

    /*Connect to ccnd*/

    ccn = ccn_create();

    if (ccn_connect(ccn, NULL) == -1)
    {
        mylog("Could not connect to ccnd");
        exit(1);
    }

	/*Create thread for receiving stream*/
	rc = pthread_create(&stream_thread, NULL, receive_stream, (void *)t);

	if(rc)
	{
		mylog("Error: pthread_create()");
		exit(-1);
	}

	
    /*Create charbufs*/
    name = ccn_charbuf_create();
    pname = ccn_charbuf_create();
    temp = ccn_charbuf_create();
	locbuffer = ccn_charbuf_create();
	
	/*Content type is data*/
    content_type = CCN_CONTENT_DATA;



    /*Get URI from parameter*/
    res = ccn_name_from_uri(name, argv[1]);

    if (res < 0)
    {
        fprintf(stderr, "%s: bad ccn URI: %s\n", progname, argv[0]);
        exit(1);
    }

    ccn_charbuf_append(pname, name->buf, name->length);

    res = ccn_set_interest_filter(ccn, pname, &in_interest);
	
    if (res < 0)
    {
        mylog("Failed to register interest");
        exit(1);
    }



    res = ccn_run(ccn, -1);



    if (in_interest.intdata == 0)
    {
        mylog("Interest timeout");
        exit(1);
    }
    else
    {
        mylog("Intdata != 0");
    }



	pthread_exit(NULL);
    return 0;
}

