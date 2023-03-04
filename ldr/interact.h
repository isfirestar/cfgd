#if !defined NETOBJECT_H
#define NETOBJECT_H

#include "nis.h"

#include "request.h"

extern int jnet_startup(const char *ip, unsigned short port);
extern int jnet_write_response(HTCPLINK link, const unsigned char *data, int size);

extern void jnet_subscribe(JRP *jrp);
extern void jnet_unsubscribe(JRP *jrp);
extern void jnet_publish(JRP *jrp);

#endif
