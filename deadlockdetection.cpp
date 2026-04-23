
#include <stdio.h>

#include "deadlockdetection.h"

bool deadlock( const int *available, const int m, const int n, const int *request, const int *allocated) {
    int work[m];   // m resources
    bool finish[n];   // n processes

    for (int i (0); i < m; i++)
         work[i] = available[i];
    for (int i (0); i < n; i++)
        finish[i] = false;

    int p (0);
    for (; p < n; p++) {
        if ( finish[p] ) continue;
        if (req_lt_avail (request, work, p, m)) {
            finish[p] = true;
            for (int i (0); i < m; i++)
                work[i] += allocated[p*m+i];
            p = -1;
        }
    }

    // print out those that are deadlocked
    printf("Finished processes array\n ["); 
    for (p = 0; p < n; p++) {
        printf("%d ",finish[p]); 
    }
    printf("]\n");

    // deadlocked processes did not finish
    for (p = 0; p < n; p++)
        if ( !finish[p] )
            break;

    return (p != n);
}

bool req_lt_avail(const int *req, const int *avail, const int pnum, const int num_res) {
    int i (0);
    for (; i < num_res; i++)
        if (req[pnum*num_res+i] > avail[i] )
            break;
    return ( i == num_res );
}

