#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ccn/ccn.h>
#include <ccn/ccnd.h>
#include <ccn/uri.h>

#include <pthread.h>

/*OpenSSL stuff*/
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>



#define SRV_IP "127.0.0.1"
#define SRV_PORT 1240
#define BUFLEN 1316
#define NPACK 16000

char *progname;
unsigned char **media_buffer;
pthread_mutex_t lock;
unsigned short current_slot, indice;
short idx = 0, oldest = 0;
unsigned long written = 0, _read = 0;


static void mylog(char *msg)
{
    printf("[%s]: %s\n", progname, msg);
}

static void print_as_hex (const unsigned char *digest, int len) 
{	
  	int i;
  	for(i = 0; i < len; i++)
	{
    	printf ("%02x", digest[i]);
  	}
}

static void shift_array(unsigned char *array, short amount)
{
	int i;
	
	for(i = 1; i < NPACK; i++)
	{

		array[i] = array[i + 1]; 

	}
	current_slot = NPACK - 1;
}
static unsigned char hash_packet(unsigned char *data, size_t data_len)
{
	EVP_MD_CTX mdctx;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	unsigned int md_len;

	EVP_DigestInit(&mdctx, EVP_md5());
	EVP_DigestUpdate(&mdctx, data, (size_t) data_len);
	EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
	EVP_MD_CTX_cleanup(&mdctx);
	print_as_hex (md_value, md_len);
	return md_value;

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
	printf("le incoming interest\n");
    int res = 0, i = 0;
    struct ccn_charbuf *cob;
	struct ccn_charbuf *temp, *pname, *name;
    enum ccn_content_type content_type = CCN_CONTENT_DATA;
    struct ccn_signing_params sp = CCN_SIGNING_PARAMS_INIT;
	unsigned char *buf, *nack;
	
	sp.type = content_type;
	
	temp = ccn_charbuf_create();
	pname = ccn_charbuf_create();
	name = ccn_charbuf_create();
	
	/*Get URI from parameter*/
	/*Changed info->interest_comps to info->interest_combs->n -- change if it doesnt work --*/

    res = ccn_uri_append(pname, info->interest_ccnb, (int)info->interest_comps, 1);
	ccn_name_from_uri(name, ccn_charbuf_as_string(pname));
	

	
    if (res < 0)
    {
		
        fprintf(stderr, "%s: bad ccn URI: %s\n", progname, ccn_charbuf_as_string(pname));
        exit(1);
    }
	
	//printf("Interest name: %s\n", ccn_charbuf_as_string(pname));
	
	temp->length = 0;
	

	
	//printf("Attempting to sign...\n");
	
	pthread_mutex_lock(&lock);  
	

	/*Signing should be done here*/
	
	printf("Oldest: %d\n", oldest);
	if(_read >= written)
	{
		nack = malloc(sizeof(unsigned char)*5);
		
		printf("Delivering nack\n", nack);
		res = ccn_sign_content(info->h, temp, name, &sp, nack, sizeof(unsigned char)*5);   
		free(nack);
	}
	else
	{
    		
		printf("Delivering index:%d Packet number:%s\n", oldest, media_buffer[oldest]);
		res = ccn_sign_content(info->h, temp, name, &sp, media_buffer[oldest++], BUFLEN);   
		_read++;
	}


	pthread_mutex_unlock(&lock);  
	
    switch (kind)
    {
    case CCN_UPCALL_FINAL:
       // mylog("CCN_UPCALL_FINAL");
        break;
    case CCN_UPCALL_INTEREST:
	 // mylog("CCN_UPCALL_INTEREST");


	
				
        if (ccn_content_matches_interest(temp->buf, temp->length,
                                         1, NULL,
                                         info->interest_ccnb, info->pi->offset[CCN_PI_E],
                                         info->pi))
        {
           // mylog("Incoming interest");
            res = ccn_put(info->h, temp->buf, temp->length);
            if(res >= 0)
            {
               // mylog("Successful");
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
        //mylog("default");
        break;
    }

    return(CCN_UPCALL_RESULT_OK);
}


void *receive_stream(void *threadid)
{
	unsigned char fill = 0;
    struct sockaddr_in si_me, si_other;
    int s, ret, slen = sizeof(si_other);
	unsigned long packet_count = 0;
    char buf[BUFLEN];
	unsigned char *hash;
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


	printf("Packets Received\t\tPacket Number\t\tBuffer Position\n");
    while((bytes_read = recvfrom(s, buf, BUFLEN, 0, &si_other, &slen)) != -1)
    {

		/*Print md5 hash of packet*/
		//hash = hash_packet (buf, BUFLEN);
		

		
        /*Deploy current packet within global circular buffer*/
		pthread_mutex_lock(&lock);   //Mutex -> important do not remove
		memcpy(media_buffer[idx], buf, bytes_read);
		pthread_mutex_unlock(&lock); 
		printf("%lu\t\t        %lu\t\t %d \n", packet_count, atoi(buf), bytes_read);
		idx++;
		written++;
		
		
		packet_count++;
		
		

		bytes_total += bytes_read;
		

		if(packet_count % 500 == 0)
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
	int rc, i = 0;
	long t = 0;

	/*complex mutex stuff*/
	pthread_mutex_init(&lock, NULL);

	/*Allocate buffer space*/
	media_buffer = malloc(NPACK * sizeof(unsigned char *));
	for(i = 0; i < NPACK; i++)
	{
		media_buffer[i] = malloc(BUFLEN);
	}


	current_slot = 0;
	indice = 0;
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

