
#ifndef DEADLOCKDETECTION_H
#define DEADLOCKDETECTION_H 


bool deadlock( const int *available, const int m, const int n, const int *request, const int *allocated); 


bool req_lt_avail(const int *req, const int *avail, const int pnum, const int num_res); 

#endif


