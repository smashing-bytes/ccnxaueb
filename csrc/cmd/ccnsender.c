/**
 * @file cmd/ccnsender
 * 
 * Reads standard RTP/UDP streams from specified URI and outputs the stream into CCNx packets.
 *
 * Copyright (C) 2009, 2010, 2011 Palo Alto Research Center, Inc.
 *
 * This work is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 * This work is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details. You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>

#include <ccn/ccn.h>
#include <ccn/ccnd.h>


enum ccn_upcall_res incoming_content(struct ccn_closure *selfp,
                                     enum ccn_upcall_kind kind,
                                     struct ccn_upcall_info *info)
{
    printf("Incoming content in ccnsender!\n");
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

    //ccn_express_persistent_interest(ccn, name, incoming, NULL, 1);
    ccn_express_interest(ccn, name, incoming, NULL);



    printf("Interests send\n", res);

    res = ccn_run(ccn, 10000);

    return 0;
}

