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

#define SRV_IP "127.0.0.1"
#define SRV_PORT 1234
#define BUFLEN 1344
#define NPACK 10

char *progname;



static void mylog(char *msg)
{
    printf("[%s]: %s\n", progname, msg);
}

char *get_interest_name(struct ccn_upcall_info *info)
{
	const unsigned char *comp;
	size_t comp_size;
	struct ccn_charbuf *c;
	char *name;
	struct ccn_indexbuf *comps;
	ssize_t l;
	int i;
	
	comps = ccn_indexbuf_create();
	ccn_parse_interest(info->interest_ccnb, sizeof(info->interest_ccnb), info->pi, comps);

	 /* Name */
    c = ccn_charbuf_create();
    ccn_uri_append(c, info->interest_ccnb, sizeof(info->interest_ccnb), 1);

	printf("%s", ccn_charbuf_as_string(c));
	for (i = 0; i < comps->n - 1; i++) 
	{
        ccn_name_comp_get(info->interest_ccnb, comps, i, &comp, &comp_size);
        printf("%s", comp);
    }
    return ccn_charbuf_as_string (c);
}

enum ccn_upcall_res incoming_interest(struct ccn_closure *selfp,
                                      enum ccn_upcall_kind kind,
                                      struct ccn_upcall_info *info)
{
    int res, i;
    struct ccn_charbuf *cob = selfp->data;

	

	//printf("Interest: %s \n", get_interest_name(info));
    printf("Buffer: %s, size: %d\n", ccn_charbuf_as_string (cob), cob->length);
	
    switch (kind)
    {
    case CCN_UPCALL_FINAL:
        mylog("CCN_UPCALL_FINAL");
        break;
    case CCN_UPCALL_INTEREST:
        mylog(get_interest_name(info));
        if (ccn_content_matches_interest(cob->buf, cob->length,
                                         1, NULL,
                                         info->interest_ccnb, info->pi->offset[CCN_PI_E],
                                         info->pi))
        {
            mylog("Incoming interest");
            res = ccn_put(info->h, cob->buf, cob->length);

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


static int recRTSP()
{

    struct sockaddr_in si_me, si_other;
    int s, ret, slen = sizeof(si_other);

    char buf[BUFLEN];

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

        //buf

    }

    close(s);
    return 0;
}

int main(int argc, char **argv)
{
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

    /*TODO: mix with udp stream*/
    //recRTSP();

    /*Create charbufs*/
    name = ccn_charbuf_create();
    pname = ccn_charbuf_create();
    temp = ccn_charbuf_create();
	
	
    content_type = CCN_CONTENT_DATA;


    /*Get URI from parameter*/
    res = ccn_name_from_uri(name, argv[1]);

    if (res < 0)
    {
        fprintf(stderr, "%s: bad ccn URI: %s\n", progname, argv[0]);
        exit(1);
    }

    ccn_charbuf_append(pname, name->buf, name->length);

    /*Create and sign content object*/
    buf = malloc(sizeof(char) * 50);
    strcpy(buf, "Demo buffer");

    sp.type = content_type; //Set content type
	temp->buf = buf;
   /* res = ccn_sign_content(ccn, temp, name, &sp, buf, sizeof(char)*15);
    if (res != 0)
    {
        fprintf(stderr, "Failed to encode ContentObject (res == %d)\n", res);
        exit(1);
    }*/

    in_interest.data = temp;

    printf("%s\n", argv[1]);
    res = ccn_set_interest_filter(ccn, pname, &in_interest);

    if (res < 0)
    {
        mylog("Failed to register interest");
        exit(1);
    }



    res = ccn_run(ccn, 10000);

    if (in_interest.intdata == 0)
    {
        mylog("Interest timeout");
        exit(1);
    }
    else
    {
        mylog("Intdata != 0");
    }



    return 0;
}

