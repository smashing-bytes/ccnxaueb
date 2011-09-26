#include <stdio.h>
#include <stdlib.h>

#include <ccn/ccn.h>
#include <ccn/ccnd.h>

enum ccn_upcall_res incoming_content(struct ccn_closure *selfp,
                                     enum ccn_upcall_kind kind,
                                     struct ccn_upcall_info *info)
{
    printf("Incoming content in ccn_test!\n");
    return(CCN_UPCALL_RESULT_OK);
}

int main()
{
    struct ccn *ccn = NULL;
    struct ccn_charbuf *name = NULL;
    int res;
    struct ccn_closure *incoming = NULL;

    ccn = ccn_create();
    if (ccn_connect(ccn, NULL) == -1)
    {
        perror("Could not connect to ccnd");
        exit(1);
    }

    incoming = calloc(1, sizeof(*incoming));
    incoming->p = &incoming_content;
    name = ccn_charbuf_create();

    res = ccn_name_from_uri(name, "ccnx:/FOX/test");

    ccn_express_persistent_interest(ccn, name, incoming, NULL, 1);
    //ccn_express_interest(ccn, name, incoming, NULL);



    printf("Interests send\n", res);

    res = ccn_run(ccn, 10000);

    return 0;
}


