//
//  main.cpp
//  TFTP_to_HTTP
//
//  Created by Alex on 19/09/2017.
//  Copyright © 2017 k4. All rights reserved.
//

// #define USE_FORK


//  yeah... 11pm... time for beautiful code :) coffee....i'll hope to you.
//  without pthread...without cpplibrary...omgwtf? for openwrt / lede :)



#include <time.h>
#include <stdio.h> //printf
#include <string.h> //memset
#include <stdlib.h> //exit(0);
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h> //fork
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>


#ifndef true
#define true (1==1)
#endif

#ifndef false
#define false (1!=1)
#endif

#ifndef bool
#define bool int
#endif

//namespace TFTP_TO_HTTP{

    const char* param_http_host=NULL;
    const char* param_http_port=NULL;
    const char* param_http_prefix=NULL;
    const char* param_bind_port=NULL;
    const char* param_bind_addr=NULL;

    int fatal(bool use_perror,const char* str){
        if(use_perror){perror(str);}else{printf("Fatal error: %s\n",str);}
        exit(-1);
    }
    
    int create_just_udp_socket(bool fatal_if_error){
        int sockfd;
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
            if(fatal_if_error){fatal(true,"Can't create socket");}else{return 0;}
        }
        int enable = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
            perror("setsockopt(SO_REUSEADDR) failed");
        }
        return sockfd;
    }
    
#ifndef log
    void log(const char* str){
        printf("LOG: %s\n",str);
    }
#endif



    
    int create_udp_server_socket(bool fatal_if_error,const char* bind_addr,int port){
        /* return zero if fail and fatal_if_error set to false. exit(-1) if fatal_if_error is true */
        int sockfd=create_just_udp_socket(fatal_if_error);
        struct sockaddr_in server;
        
        //bind it
        memset(&server, 0, sizeof server);
        server.sin_family = AF_INET; // Use IPv4
        server.sin_addr.s_addr = inet_addr(bind_addr); // My IP
        server.sin_port = htons(port); // Server Port
        if( bind(sockfd , (struct sockaddr*)&server, sizeof(struct sockaddr_in) ) == -1)
        {
            if(fatal_if_error){fatal(true,"Can't bind socket");}
        }
        return sockfd;
    }
    
    
    typedef struct{
        int opcode:16;
    }__attribute__((packed)) tftp_request_header_t;

    typedef struct{
        int opcode:16;
        int packet_id:16;
    }__attribute__((packed)) tftp_rrq_response_header_t;

    typedef struct{
        int opcode:16;
        int packet_id:16;
    }__attribute__((packed))  tftp_ack_hdr_t;

    typedef struct{
        int opcode:16;
        int err_code:16;
        char err_str[32];
        char null_str;
    }__attribute__((packed))  tftp_err_resp_t;

    
    const char** explode_zeroend_string(const char* from,int max_len,int max_count){ //return only pointers. please, free this ptr after use.
        //last ptr everytime will be zero.
        
        const char** rrq_strings = (const char**)malloc(sizeof(char*)*(max_count+1));
        memset(rrq_strings,0x00,sizeof(char*)*(max_count+1));
        int rrq_string_id=0;

        const char* ptr_to_write=from;
        const char* seeker=from;
        const char* seeker_max=from+max_len;
        for(;seeker<seeker_max;seeker++){
            if(*seeker==0x00){
                rrq_strings[rrq_string_id]=ptr_to_write;
                ptr_to_write=seeker+1;
                rrq_string_id++;
                if(rrq_string_id==max_count){break;}
            }
        }
        return rrq_strings;
    }
    
    void inc_packet_id(tftp_rrq_response_header_t * rh){
        rh->packet_id=htons(ntohs(rh->packet_id)+1);
    }
    
     in_addr_t ADDR_NOT_FOUND;
    in_addr_t get_addr_by_host(const char* host){

        in_addr_t address;
        
        if(inet_pton(AF_INET, host, &address)==1)
        {
            return address;
        }

        struct hostent * record = gethostbyname(host);
        
        if(record == NULL)
        {
            
            return ADDR_NOT_FOUND;
        }
        address = ( (struct in_addr * )record->h_addr )->s_addr;
        return address;
    }
    
    
    int create_tcp_socket_and_connect_to_host(const char* host,int port){ //return zero if error
        struct sockaddr_in address;
        in_addr_t remote_ip=get_addr_by_host(host);
        if(remote_ip==ADDR_NOT_FOUND){
            printf("%s is unavailable\n", host);
            return 0;
        }
        
        int sockfd;
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            return 0;
        }
        
        memset(&address, 0x00, sizeof(address));
        address.sin_family=AF_INET;
        address.sin_addr.s_addr=remote_ip;
        address.sin_port=htons(port);
        if (connect(sockfd, (struct sockaddr *)&address, sizeof(address)) != 0 )
        {
            close(sockfd);
            return 0;
        }else{
            return sockfd;
        }
    }
    
    
    bool http_send_header(int sock,const char* host,const char* prefix,const char* path){
        if(strlen(host)>128){return false;}
        if(strlen(prefix)>512){return false;}
        if(strlen(path)>512){return false;}
        char buff[8192];
        
        const char* http_mask=__STRING(GET %s%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: TFTP_TO_HTTP (by k4 / v0.0.alpha-xD)\r\nConnection: close\r\n\r\n);
        
        sprintf(buff,http_mask,prefix,path,host);
//        printf("sending header='%s'\n",buff);
        size_t sl=strlen(buff);
        if(send(sock, buff, sl, 0)==sl){
            return true;
        }
        return false;
    }
    
    bool http_read_response_header(int sock){
        char byte;
        bool ret=false;
        int r_count=0;
        int n_count=0;
        char line[1024];
        int line_pos=0;
        while(recv(sock, &byte, 1, 0)==1){
            if(byte=='\r'){
                r_count++;
            }else if(byte=='\n'){
                n_count++;
                line[line_pos++]=0;
//                printf("line=%s\n",line);
                if(strstr(line, "HTTP/1.1 404")!=NULL){
                    printf("File not found\n");
                    return false;
                }else if(strstr(line, "chunked")!=NULL){
                    printf("chunked proto not support\n");
                    return false;
                }
                line_pos=0;
            }else{
                r_count=n_count=0;
                line[line_pos++]=byte;
                if(line_pos==1000){
                    return false;
                }
            }
            if(n_count==2){
                ret=true;
                break;
            }
        }
        printf("File found\n");
        return ret;
    }
    
    int request_http_with_filename(const char* filename){
        int http_sock=create_tcp_socket_and_connect_to_host(param_http_host,atoi(param_http_port));
//        printf("http_sock=%d required filename=%s\n",http_sock,filename);
        if(http_sock){
            if(http_send_header(http_sock,param_http_host,param_http_prefix,filename)){
                if(http_read_response_header(http_sock)){
                    return http_sock;
                    /*
                    printf("ready to read response\n");
                    char http_buff[8192];
                    while(true){
                        size_t readed_http_len=recv(http_sock,&http_buff[0],8192,0);
                        if(readed_http_len!=-1 && readed_http_len>0){
                            printf("readed %d bytes\n",readed_http_len);
                        }else{
                            break;
                        }
                    }
                    */
                }
            }
            close(http_sock);
        }
        return 0;
    }
    
    #include <sys/ioctl.h>
    
    ssize_t recv_e(int sock,char* buf,int len,int flags){
        ssize_t ret=0;
        while(len-ret){
            ssize_t rr=recv(sock,&buf[ret],len-ret,flags);
            if(rr==-1 || rr==0){break;}
            ret+=rr;
        }
//        printf("recv_e %d\n",ret);
        return ret;
    }
    
    void clear_udp_socket(int sockfd){
     int count;
        char tmp[8192];
        while(true){
            ioctl(sockfd, FIONREAD, &count);
            if(count){
                printf("avail data %d bytes\n",count);
                socklen_t sal=sizeof(struct sockaddr_in);
                struct sockaddr_in sa;
                int rf=recvfrom(sockfd, &tmp[0], 8192, 0, (struct sockaddr*)&sa, &sal);
                printf("rf=%d\n",rf);
            }else{
                break;
            }
        }
    }

    void tftp_server_loop(const char* bind_addr){
        int srv_socket=create_udp_server_socket(true,bind_addr,atoi(param_bind_port));
        static int buflen=8192;
        char * buf = (char*)malloc(buflen);
        struct sockaddr_in received_sockaddr;
        socklen_t received_socklen;
        ssize_t received_data_len;
        tftp_request_header_t * tftp_request_header;
        printf("TFTP_TO_HTTP server started.\n");
        while(true){
            received_socklen=sizeof(struct sockaddr_in);
            if ((received_data_len = recvfrom(srv_socket, buf, buflen, 0, (struct sockaddr *) &received_sockaddr, &received_socklen)) == -1)
            {
                if(errno!=EAGAIN){
                    perror("recvfrom return -1. why? ");
                    exit(-1);
                    break;
                }
            }
            if(received_data_len<sizeof(tftp_request_header_t)){
                //bad request;
                log("bad request");
            }else{
                tftp_request_header=(tftp_request_header_t*)buf;
                
                if(htons(tftp_request_header->opcode)==1){
                    //RRQ...whell..'ll be parse
                    const char** exploded=explode_zeroend_string(buf+2, ((int)received_data_len-2), 32 /* divide by 2! */);
                    const char* rrq_filename=exploded[0];
                    const char* rrq_get_mode=exploded[1];
                    int blksize=512;
                    for(int i=2;exploded[i] && exploded[i+1];i+=2){
//                        if(strcmp(exploded[i],"blksize")==0){blksize=atoi(exploded[i+1]);}
//                        printf("rrq data = %s\n",exploded[i]);
                    }
                    char requested_file[8192];
                    //checking mzfka
                    if(blksize>0 && blksize<4096 && rrq_filename && rrq_get_mode && strcmp(rrq_get_mode,"octet")==0 && strlen(rrq_filename)<512 ){
                        //okay. fine...
                        printf("requested file '%s' with mode '%s' bs='%d' \n",rrq_filename,rrq_get_mode,blksize);
#ifdef USE_FORK
                        pid_t pid = fork();
                        if(pid==0){
#endif
                            //i'm child
//                            printf("HELLO FROM FORK! \n");
                            sprintf(requested_file,"%s",rrq_filename);

                            int http_socket;
                            http_socket=request_http_with_filename(rrq_filename);
                            
                            //1'st clean up this room
#ifdef USE_FORK

                            free(exploded); //dont put it w (else=parent)
                            close(srv_socket);
#endif
#ifndef USE_FORK
                            time_t started_time=time(NULL);
#endif
                            
                            //http. you time now
                            //tftp response create
                            tftp_rrq_response_header_t * rrq_resp_header=(tftp_rrq_response_header_t*)buf;
                            ssize_t sendto_or_recvfrom_result;
                            socklen_t sd_received_socklen;
                            struct sockaddr_in sd_received_sockaddr;
                            

                            //test s

                            rrq_resp_header->opcode=htons(3);
                            rrq_resp_header->packet_id=0;

                            tftp_ack_hdr_t * tftp_ack_hdr;
                            
                            int socket_sender=create_just_udp_socket(false);
                            struct timeval timeout;
                            timeout.tv_sec = 0;
                            timeout.tv_usec = 500000;
                            setsockopt(socket_sender, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                            if(socket_sender){
//                                printf("HTTP - you time now!\n");
                                if( ( http_socket )  !=0 ){
                                    size_t received_http_len=0;
                                    while((received_http_len=recv_e(http_socket,buf+sizeof(tftp_rrq_response_header_t),blksize,0))>0){
                                    //SENDING DATA LOOP START
                                        inc_packet_id(rrq_resp_header);
//                                        printf("send from 1\n");
                                        sendto_or_recvfrom_result=sendto(socket_sender, buf, received_http_len+sizeof(tftp_rrq_response_header_t), 0, (struct sockaddr *) &received_sockaddr, received_socklen);
                                        bool received_ack=false;
                                        int timeo_count=0;
                                        while(!received_ack){//wait ack loop
                                            char ack_buff[1024];
                                            tftp_ack_hdr=(tftp_ack_hdr_t*)ack_buff;
                                            received_ack=false;
                                            sd_received_socklen=sizeof(sd_received_sockaddr);
                                            sendto_or_recvfrom_result=recvfrom(socket_sender, &ack_buff[0], sizeof(ack_buff), 0, (struct sockaddr *) &sd_received_sockaddr, &sd_received_socklen);

                                            if(sendto_or_recvfrom_result!=-1){
                                                timeo_count=0;
                                            }
                                            
                                            if(sendto_or_recvfrom_result!=-1 && sendto_or_recvfrom_result>=sizeof(tftp_ack_hdr_t)){
                                                if(htons(tftp_ack_hdr->opcode)==4){
                                                    received_ack=true;
                                                }else if(htons(tftp_ack_hdr->opcode)==5){
                                                    tftp_err_resp_t* err=(tftp_err_resp_t*)ack_buff;
                                                    err->null_str=0;
                                                    printf("received error opcode: %d - %s\n",err->err_code,&err->err_str[0]);
                                                    received_ack=false;
                                                    break;
                                                }else{
                                                    printf("received bad opcode: %d\n",htons(tftp_ack_hdr->opcode));
                                                    received_ack=false;
                                                    break;
                                                }
                                            }else if(sendto_or_recvfrom_result==-1){
                                                if(errno!=ETIMEDOUT)
                                                {
                                                    received_ack=false;
                                                    break;//break wait loop ack
                                                }else{
                                                    timeo_count++;
                                                    if(timeo_count==2){
                                                        received_ack=false;
                                                        break;//break wait loop ack
                                                    }else{
                                                        //retry send packet
                                                        //                                                        printf("send from 2\n");
                                                        sendto_or_recvfrom_result=sendto(socket_sender, buf, received_http_len+sizeof(tftp_rrq_response_header_t), 0, (struct sockaddr *) &received_sockaddr, received_socklen);
                                                        if(sendto_or_recvfrom_result==-1){
                                                            received_ack=false;
                                                            break;
                                                        } // send err
                                                    }//timeout count =2
                                                }  //else timeo
                                            }//==-1
                                           
                                        }//wait ack loop
                                        if(received_http_len!=blksize){
                                    //        printf("received_http_len < blksize. fnished?\n");
                                            //if(blksize>512){
                                            //    inc_packet_id(rrq_resp_header);
                                            //    sendto(socket_sender, buf, sizeof(tftp_rrq_response_header_t), 0, (struct sockaddr *) &received_sockaddr, received_socklen);
                                            //}
                                            break;
                                        }
                                        if(!received_ack){
                                      //      printf("ack not received\n");
                                            break;
                                        }else{
                                        //    printf("ack received\n");
                                        }
                                        //SENDING DATA LOOP END
                                        
                                    }//==blksize while
                                    printf("closing http (%s)\n",requested_file);
                                    close(http_socket);
                                }//http_socket != 0
                                else{//http_socket != 0
                                    tftp_err_resp_t err_resp;
                                    err_resp.opcode=htons(5);
                                    err_resp.err_code=404;
                                    err_resp.null_str=0;
                                    sprintf(err_resp.err_str,"http_err");
                                    sendto(socket_sender, &err_resp, sizeof(err_resp), 0, (struct sockaddr *) &received_sockaddr, received_socklen);
                                }
                                
                                close(socket_sender);
#ifndef USE_FORK
                                time_t ending_time=time(NULL);
                                if(ending_time-started_time>0){
                                    clear_udp_socket(srv_socket);
                                }
#endif
                            }
                            //test e
#ifdef USE_FORK
                            free(buf);
                            exit(0); //if not - you'll be in main recvdata loop
#endif
#ifdef USE_FORK
                        }else if(pid==-1){
                            perror("cannot fork");
                        }else{
                            //i'm parent.
                        }
#endif
                    }
                    free(exploded);
                }else{
                    //fkk off. i don't know you!
                }
            }
            
        }
        free(buf);
    }
    
    
    void parse_commandline(int argc,const char** argv){
        for(int i=1;(i+1)<argc;i+=2){
            if(strcmp(argv[i],"--http_host")==0){
                param_http_host=argv[i+1];
            }else if(strcmp(argv[i],"--http_port")==0){
                param_http_port=argv[i+1];
            }else if(strcmp(argv[i],"--http_prefix")==0){
                param_http_prefix=argv[i+1];
            }else if(strcmp(argv[i],"--bind_port")==0){
                param_bind_port=argv[i+1];
            }else if(strcmp(argv[i],"--bind_addr")==0){
                param_bind_addr=argv[i+1];
            }
        }
        
    }

    void check_commandline(){
        if(!param_http_prefix){printf("--http_prefix not set. exiting\n");exit(-1);}
        if(!param_http_host){printf("--http_host not set. exiting\n");exit(-1);}
        if(!param_http_port){printf("--http_port not set. exiting\n");exit(-1);}
        if(!param_bind_addr){printf("--bind_addr not set. exiting\n");exit(-1);}
        if(!param_bind_port){printf("--bind_port not set. exiting\n");exit(-1);}
        if(strlen(param_http_prefix)>512){printf("--http_prefix bad value. exiting\n");exit(-1);}
        if(strlen(param_http_host)>128){printf("--http_host bad value. exiting\n");exit(-1);}
        if(strlen(param_http_port)>10){printf("--http_port bad value. exiting\n");exit(-1);}
        if(strlen(param_bind_addr)>20){printf("--bind_addr bad value. exiting\n");exit(-1);}
        if(strlen(param_bind_port)>10){printf("--bind_port bad value. exiting\n");exit(-1);}
    }
    
    
//};//namespace

//using namespace TFTP_TO_HTTP;

int main(int argc, const char * argv[]) {
    ADDR_NOT_FOUND=inet_addr("0.0.0.0");
    srand((int)time(NULL));
    signal(SIGCHLD,SIG_IGN);
    signal(SIGPIPE,SIG_IGN);
    parse_commandline(argc,argv);
    check_commandline();
    tftp_server_loop(param_bind_addr);
    // insert code here...
    return 0;
}
