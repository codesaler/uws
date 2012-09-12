#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "uws_fastcgi.h"
#include "uws_header.h"
#include "uws_config.h"
#include "uws_error.h"
#include "uws_utils.h"
#define PARAMS_BUFF_LEN     1024

char *mem_file = NULL;
memory_t smem;
int file_len = 0;
void
write_mem_file(char *content, unsigned long length) 
{
    mem_file = (char*) realloc(mem_file, length + file_len);
    memcpy(mem_file + file_len, content, length);
    file_len += length;
}
Param_Value*
read_header(char *mem, unsigned long length)
{
    return NULL;
}


static FCGI_Header
make_header(int type, int request_id, int content_len, int padding_len)
{
    FCGI_Header header;
    header.version          =           FCGI_VERSION_1;
    header.type             =           (unsigned char) type;
    header.requestIdB1      =           (unsigned char) ((request_id >> 8) & 0xff);
    header.requestIdB0      =           (unsigned char) (request_id & 0xff);
    header.contentLengthB1  =           (unsigned char) ((content_len >> 8) & 0xff);
    header.contentLengthB0  =           (unsigned char) (content_len & 0xff);
    header.paddingLength    =           (unsigned char) padding_len;
    header.reserved         =           0;
    return header;
}
static FCGI_BeginRequestBody
make_begin_request_body(int role, int keep_conn)
{
    FCGI_BeginRequestBody body;
    body.roleB1         =           (unsigned char) ((role >> 8) & 0xff);
    body.roleB0         =           (unsigned char) (role & 0xff);
    body.flags          =           (unsigned char) (keep_conn ? FCGI_KEEP_CONN : 0);
    memset(body.reserved, 0, sizeof(body.reserved));
    return body;
}

static void
build_name_value_body(char *name, int name_len, char *value, int value_len, unsigned char *body_buff, int *body_len)
{
    unsigned char *start_body_buff = body_buff;
    if( name_len < 0x80) {
        *body_buff++ = (unsigned char) name_len;
    } else {
        *body_buff++ = (unsigned char) ((name_len >> 24) | 0x80);
        *body_buff++ = (unsigned char) (name_len >> 16);
        *body_buff++ = (unsigned char) (name_len >> 8);
        *body_buff++ = (unsigned char) name_len;
    }

    if( value_len < 0x80) {
        *body_buff++ = (unsigned char) value_len;
    } else {
        *body_buff++ = (unsigned char) ((value_len >> 24) | 0x80);
        *body_buff++ = (unsigned char) (value_len >> 16);
        *body_buff++ = (unsigned char) (value_len >> 8);
        *body_buff++ = (unsigned char) value_len;
    }
    while(*name != '\0') *body_buff++ = *name++;
    while(*value != '\0') *body_buff++ = *value++;
    *body_len = body_buff - start_body_buff;
}

static void
add_fcgi_param(int sockfd, int request_id, char* name, char* value) {
    int name_len, value_len, body_len, count;
    unsigned char body_buff[PARAMS_BUFF_LEN];
    bzero(body_buff, PARAMS_BUFF_LEN);
    name_len        =           strlen(name);
    value_len       =           strlen(value);
    build_name_value_body(name, name_len, value, value_len, &body_buff[0], &body_len);
    
    FCGI_Header name_value_header;
    name_value_header = make_header(FCGI_PARAMS, request_id, body_len, 0);

    int name_value_record_len = body_len + FCGI_HEADER_LEN;
    char name_value_record[name_value_record_len];
    memcpy(name_value_record, (char*)&name_value_header, FCGI_HEADER_LEN);
    memcpy(name_value_record + FCGI_HEADER_LEN, body_buff, body_len);
    append_mem(&smem, (char *)&name_value_record, name_value_record_len);
    //count = write(sockfd, (char*)&name_value_record, name_value_record_len);
}
bool
send_request(const char* host, int port, Param_Value init_pv[], memory_t *stdin_data)
{
    int sockfd,
        result,
        count,
        request_id = 1;
    Param_Value* pv = init_pv;
    struct sockaddr_in address;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(host);
    address.sin_port = htons(port);
    result = connect(sockfd, (struct sockaddr*)&address, sizeof(address));
    if(result == -1) {
        return false;
    }
    FCGI_BeginRequestRecord begin_record;
    begin_record.header = make_header(FCGI_BEGIN_REQUEST, request_id, sizeof(begin_record.body), 0);
    begin_record.body = make_begin_request_body(FCGI_RESPONDER, 0);

    append_mem(&smem, (char *)&begin_record, sizeof(begin_record));
    //count = write(sockfd, (char *)&begin_record, sizeof(begin_record));

    while(pv->name != NULL){
        add_fcgi_param(sockfd, request_id, pv->name, pv->value);
        pv++;
    }

    //terminate params
    FCGI_Header end_params;
    end_params = make_header(FCGI_PARAMS, request_id, 0, 0);
    append_mem(&smem, (char *)&end_params, FCGI_HEADER_LEN);
    //write(sockfd, (char *)&end_params, FCGI_HEADER_LEN);
    //------ body TODO:content bigger than MAX_STDIN_SIZE
    FCGI_Header content_body;
    content_body = make_header(FCGI_STDIN, request_id, stdin_data->len, 0);
    append_mem(&smem, (char *)&content_body, FCGI_HEADER_LEN);
    //write(sockfd, (char *)&content_body, FCGI_HEADER_LEN);
    append_mem(&smem, stdin_data->mem, stdin_data->len);
    //count = writen(sockfd, stdin_data->mem, stdin_data->len);
    //terminate stdin
    FCGI_Header end_body;
    end_body = make_header(FCGI_STDIN, request_id, 0, 0);
    append_mem(&smem, (char *)&end_body, FCGI_HEADER_LEN);
    //write(sockfd, (char *)&end_body, FCGI_HEADER_LEN);
    //------ body

    FCGI_Header end_header;
    end_header = make_header(FCGI_PARAMS, request_id, 0, 0);
    append_mem(&smem, (char*)&end_header, FCGI_HEADER_LEN);
    //count = write(sockfd, (char*)&end_header, FCGI_HEADER_LEN);
    writen(sockfd, smem.mem, smem.len);

    FCGI_Header response_header;
    char* content;
    int content_len;
    char tmp[8];
    while(read(sockfd, &response_header, FCGI_HEADER_LEN) > 0) {
        if(response_header.type == FCGI_STDOUT) {
            content_len = (response_header.contentLengthB1 << 8) + (response_header.contentLengthB0);
            content = (char*) calloc(sizeof(char), content_len);

            count = read(sockfd, content, content_len);

            write_mem_file(content, content_len);

            free(content);
            if(response_header.paddingLength > 0) {
                count = read(sockfd, tmp, response_header.paddingLength);
                if(count != response_header.paddingLength) perror("read response error");
            }
        }
        else if(response_header.type == FCGI_STDERR) {
            content_len = (response_header.contentLengthB1 << 8) + (response_header.contentLengthB0);
            content = (char*) malloc(content_len * sizeof(char));
            count = read(sockfd, content, count);
            fprintf(stdout, "error:%s\n", content);
            free(content);

            if(response_header.paddingLength > 0) {
                count = read(sockfd, tmp, response_header.paddingLength);
                if(count != response_header.paddingLength) perror("read");
            }
        }
        else if(response_header.type == FCGI_END_REQUEST) {
            FCGI_EndRequestBody end_request;
            count = read(sockfd, &end_request, FCGI_HEADER_LEN);

            /*
            if(count != 8) perror("read");
fprintf(stdout,"\nend_request:appStatus:%d,protocolStatus:%d\n",(end_request.appStatusB3<<24)+(end_request.appStatusB2<<16) +(end_request.appStatusB1<<8)+(end_request.appStatusB0),end_request.protocolStatus);
*/

        }
    }
    close(sockfd);
    return true;
}

int
fastcgi_router(int sockfd) 
{
    char *port = itoa(running_server->listen);
    Param_Value pv[] = {
        {"QUERY_STRING",request_header->request_params},
        {"REQUEST_METHOD", request_header->method},
        {"REQUEST_METHOD", request_header->method},
        {"CONTENT_TYPE", nullstring(get_header_param("Content-Type", request_header))},
        {"CONTENT_LENGTH", nullstring(get_header_param("Content-Length", request_header))},
        {"SCRIPT_FILENAME", request_header->path},
        {"SCRIPT_NAME", strrchr(request_header->path, '/')},
        {"REQUEST_URI", request_header->url},
        {"DOCUMENT_URI", request_header->path + strlen(running_server->root)},
        {"DOCUMENT_ROOT", running_server->root},
        {"SERVER_PROTOCOL", request_header->http_ver},
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SERVER_SOFTWARE", UWS_SERVER},
        {"REMOTE_ADDR", get_header_param("Client-IP", request_header)},
        {"REMOTE_PORT", get_header_param("Client-PORT", request_header)},
        {"SERVER_ADDR", server_ip},
        {"SERVER_PORT", port},
        {"SERVER_NAME", running_server->server_name},
        {"HTTPS", ""},
        {"REDIRECT_STATUS", "200"},
        {"HTTP_HOST", get_header_param("Host", request_header)},
        {"HTTP_CONNECTION", nullstring(get_header_param("Connection",request_header))},
        {"HTTP_CACHE_CONTROL", nullstring(get_header_param("Cache-Control",request_header))},
        {"HTTP_USER_AGENT", nullstring(get_header_param("User-Agent",request_header))},
        {"HTTP_ACCEPT", nullstring(get_header_param("Accept",request_header))},
        {"HTTP_ACCEPT_ENCODING", nullstring(get_header_param("Accept-Encoding",request_header))},
        {"HTTP_ACCEPT_LANGUAGE", nullstring(get_header_param("Accept-Language",request_header))},
        {"HTTP_ACCEPT_CHARSET", nullstring(get_header_param("Accept-Charset",request_header))},
        {"HTTP_REFER", nullstring(get_header_param("Refer",request_header))},
        {"HTTP_COOKIE", nullstring(get_header_param("Cookie",request_header))},
        {NULL,NULL} 
    };

    int i;
    char *fastcgi_pass = running_server->fastcgi_pass;
    char fhost[20];
    char fport[10];
    sscanf(fastcgi_pass, "%[^:]:%s", fhost, fport);
    if(!send_request(fhost, atoi(fport), pv, request_content)) {
        send_error_response(sockfd, 502, true);
    }

    //TODO:More status
    char line[LINE_LEN] = {0};
    char *oldpos = mem_file;
    char *pos;
    struct http_header fcgi_response_header;
    bzero(&fcgi_response_header, sizeof(fcgi_response_header));
    char key[LINE_LEN];
    char value[LINE_LEN];
    
    char *time_string = get_time_string(NULL);
    add_header_param("Server", UWS_SERVER, &fcgi_response_header);
    add_header_param("Date", time_string, &fcgi_response_header);
    free(time_string);

    while(pos = strstr(oldpos, "\r\n")) {
        if(oldpos == pos) break;
        bzero(line, LINE_LEN);
        strncpy(line, oldpos, pos - oldpos);
        sscanf(line, "%[^:]: %s", key, value);
        if(strcmp(key, "Status") == 0) {
            fcgi_response_header.status_code = atoi(value);
        } else {
            push_header_param(key, value, &fcgi_response_header);
        }
        oldpos = pos + strlen("\r\n");
    }
    int content_len = file_len - (pos - mem_file) - strlen("\r\n");
    char *str_len =  itoa(content_len);
    add_header_param("Content-Length", str_len, &fcgi_response_header);
    free(str_len);

    fcgi_response_header.http_ver = "HTTP/1.1";
    if(fcgi_response_header.status_code == 0) fcgi_response_header.status_code = 200;
    fcgi_response_header.status = get_by_code(fcgi_response_header.status_code);

    char *header_str = str_response_header(&fcgi_response_header);
    writen(sockfd, header_str, strlen(header_str));
    writen(sockfd, pos, content_len + strlen("\r\n"));

    free_header_params(&fcgi_response_header);
    free(mem_file);
    mem_file = NULL;
    file_len = 0;

    return 0;
}

