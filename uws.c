#include "uws.h"
#include <mcheck.h>
#include "uws_socket.h"
#include "uws_config.h"
#include "uws_router.h"
#include "uws_mime.h"
extern int
errno;
void
exit_err(const char* str) 
{
    printf("%s: %s\n", str, strerror(errno));
    exit(errno);
}

int
main(int argc, const char *argv[])
{
    setenv("MALLOC_TRACE", "output", 1);
    //mtrace();
    init_config();
    read_mime();
    init_routers();
    start_server();
    return 0;
}

