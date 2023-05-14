//�ļ���: server/stcp_server.c
//
//����: ����ļ�����STCP�������ӿ�ʵ��. 

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/select.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "stcp_server.h"
#include "../topology/topology.h"
#include "../common/constants.h"


//Defined by Niu in lab4-1
#include "unistd.h"
#include "assert.h"
#include "string.h"

server_tcb_t ** server_tcbs=NULL;//��������tcb��
int sip_conn=0; //��������son���׽����ļ�������
pthread_t seghandler_tid; //������seghandler���߳�

extern int print_index;//��ӡ�ı��
//Defined by Niu in lab4-1 end
/*********************************************************************/
//
//STCP APIʵ��
//
/*********************************************************************/

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL. �������TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, 
// �ñ�����Ϊsip_sendseg��sip_recvseg���������. ���, �����������seghandler�߳�����������STCP��.
// ������ֻ��һ��seghandler.
void stcp_server_init(int conn) 
{
    //��ʼ����������tcb����
    server_tcbs=(server_tcb_t**)malloc(MAX_TRANSPORT_CONNECTIONS*sizeof(server_tcb_t*));
    for(int tcb_index=0;tcb_index<MAX_TRANSPORT_CONNECTIONS;tcb_index++)
        server_tcbs[tcb_index]=NULL;

    //�����ص�SON���׽���������
    sip_conn=conn;

    int seghandler_err= pthread_create(&seghandler_tid,NULL,seghandler,NULL);
    pthread_detach(seghandler_tid);
}

// ����������ҷ�����TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��, ����, TCB state������ΪCLOSED, �������˿ڱ�����Ϊ�������ò���server_port. 
// TCB������Ŀ������Ӧ��Ϊ�����������׽���ID�������������, �����ڱ�ʶ�������˵�����. 
// ���TCB����û����Ŀ����, �����������-1.
int stcp_server_sock(unsigned int server_port) 
{
    //1��Ѱ���е�TCB��Ŀ��������Ӧ��tcb
    int new_tcb = tcbtable_newtcb(server_port);
    if(new_tcb<0)//ʧ�ܷ���-1
        return -1;
    //�����Ӧ��tcbָ��
    server_tcb_t * s_tcb = tcbtable_gettcb(new_tcb);

    //3����tcb������
    s_tcb->client_nodeID =-1;
    s_tcb->server_nodeID = -1;

    s_tcb->expect_seqNum = 0;
    s_tcb->state = CLOSED;

    //ָ����ջ�������ָ��
    s_tcb->recvBuf=(char *)malloc(sizeof(char)*RECEIVE_BUF_SIZE);
    memset(s_tcb->recvBuf,0,sizeof(char)*RECEIVE_BUF_SIZE);

    //���ջ��������ѽ������ݵĴ�С
    s_tcb->usedBufLen=0;

    //ָ��һ����������ָ��, �û��������ڶԽ��ջ������ķ���
    //Ϊ���ͻ���������������
    pthread_mutex_t* recvBuf_mutex;
    recvBuf_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    assert(recvBuf_mutex!=NULL);
    pthread_mutex_init(recvBuf_mutex,NULL);
    s_tcb->bufMutex = recvBuf_mutex;

    return new_tcb;
}

// �������ʹ��sockfd���TCBָ��, �������ӵ�stateת��ΪLISTENING. ��Ȼ��������ʱ������æ�ȴ�ֱ��TCB״̬ת��ΪCONNECTED 
// (���յ�SYNʱ, seghandler�����״̬��ת��). �ú�����һ������ѭ���еȴ�TCB��stateת��ΪCONNECTED,  
// ��������ת��ʱ, �ú�������1. �����ʹ�ò�ͬ�ķ�����ʵ�����������ȴ�.
int stcp_server_accept(int sockfd) 
{
    //���tcb��ָ��
    server_tcb_t* tcb=server_tcbs[sockfd];
    //תΪlistening
    tcb->state=LISTENING;
    //����æ�ȴ�����
    printf("[%d] | Server enter LISTENING .The listen port is %d\n",print_index,tcb->server_portNum);
    increase_print_index();
    while(1)
    {
        //usleep(ACCEPT_POLLING_INTERVAL);
        if(tcb->state==CONNECTED)//�����Ѿ������ɹ�
            break;
    }
    return 1;
}

// ��������STCP�ͻ��˵�����. �������ÿ��RECVBUF_POLLING_INTERVALʱ��
// �Ͳ�ѯ���ջ�����, ֱ���ȴ������ݵ���, ��Ȼ��洢���ݲ�����1. ����������ʧ��, �򷵻�-1.
int stcp_server_recv(int sockfd, void* buf, unsigned int length) 
{
    //Ѱ����Ӧ��tcb
    int recvd_length=0;
    server_tcb_t * tcb= tcbtable_gettcb(sockfd);
    if(!tcb)return -1;

    //ѭ���ȴ���֪���ռ������е��ַ��ŷ��ء�
    while(recvd_length<length)
    {
        if(tcb->usedBufLen>=length)
        {
            recvBuf_copyToClient(tcb,buf,length);
            break;
        }
        sleep(RECVBUF_POLLING_INTERVAL);
    }
    return 1;
}

// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
int stcp_server_close(int sockfd) 
{
    if(server_tcbs[sockfd]->state!=CLOSED)
        return -1;
    else
    {
        free(server_tcbs[sockfd]->bufMutex);
        free(server_tcbs[sockfd]->recvBuf);
        free(server_tcbs[sockfd]);
        server_tcbs[sockfd]=NULL;
        return 1;
    }
}

// ������stcp_server_init()�������߳�. �������������Կͻ��˵Ľ�������. seghandler�����Ϊһ������sip_recvseg()������ѭ��, 
// ���sip_recvseg()ʧ��, ��˵����SIP���̵������ѹر�, �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���.
// ��鿴�����FSM���˽����ϸ��.
void* seghandler(void* arg) 
{
    seg_t  recv_seg;
    int src_nodeID=0;//���ͷ���nodeID
    while(1)
    {
        int recv_flag=sip_recvseg(sip_conn,&src_nodeID,&recv_seg);
        if(recv_flag==-1)//�ص������Ͽ�
        {
            printf("[%d] | SON disconnected seghandler exit!\n",print_index);
            increase_print_index();
            break;
        }
        else if(recv_flag==0)//������
        {
            printf("[%d] | The package be throwed out!\n",print_index);
            increase_print_index();
            //do nothing
        }
        else if(recv_flag==1)//checksum�������
        {
            printf("[%d] | The package checksum error!\n",print_index);
            increase_print_index();
        }
        else if(recv_flag==2)//�յ��˰�
        {
            printf("The package Received!\n");
            increase_print_index();
            print_seg(&recv_seg);
            action(src_nodeID,&recv_seg);
        }
    }
    pthread_exit(NULL);
    return 0;
}


//Defined by Niu in lab4-1
void Initial_stcp_control_seg(server_tcb_t * tcb,int Signal,seg_t  *syn)
{
    //��������������Ϣ
    syn->header.src_port=tcb->server_portNum; //Դ�˿�
    syn->header.dest_port=tcb->client_portNum;//Ŀ��Ķ˿ں�

    switch(Signal) { //type
        case SYN:
            break;
        case SYNACK:
            syn->header.type = SYNACK;
            syn->header.length=0;
            break;
        case FIN:
            break;
        case FINACK:
            syn->header.type = FINACK;
            syn->header.length=0;
            break;
        case DATA:
            break;
        case DATAACK:
            syn->header.type = DATAACK;
            syn->header.seq_num=-1;        //���
            syn->header.ack_num=tcb->expect_seqNum;  //ȷ�Ϻ�
            syn->header.length=0;
            break;
        default:
            break;
    }
}

void action(int src_node,seg_t  *seg)
{
    //��Ѱ��Ӧ��sockfd��Ӧ�±꣬�õ�ָ�롣
    server_tcb_t * tcb=tcbtable_recv_gettcb(seg);
    if(!tcb)return;
    //�������Ҫ�õļ�ʱ�߳�
    pthread_t * fin_tid=NULL;
    if(tcb->state==CONNECTED&&seg->header.type==FIN)
    {
        fin_tid=(pthread_t*) malloc(sizeof(pthread_t));
    }
    //����Ҫ���͵ı���
    seg_t server_seg;
    //���ݶԷ������ı���ִ����Ӧ�Ķ���
    switch(tcb->state)
    {
        case CLOSED://do nothing
            break;
        case LISTENING:
            if (seg->header.type == SYN)
            {
                //0����һЩ�ͻ��˱��ĵ���Ϣ���������ε�tcb����
                tcb->client_portNum=seg->header.src_port;//port
                tcb->expect_seqNum=seg->header.seq_num;//��������������һ����š�
                tcb->client_nodeID=src_node; //���ÿͻ��˵�nodeID

                //1
                printf("[%d] | LISTENING :Server receive syn. Then send synback\n",print_index);
                increase_print_index();
                //2
                Initial_stcp_control_seg(tcb, SYNACK, &server_seg);

                sip_sendseg(sip_conn,src_node, &server_seg);
                //3
                tcb->state = CONNECTED;
            }
            break;
        case CONNECTED:
            switch (seg->header.type)
            {
                case SYN:
                    printf("[%d] | CONNECTED :Server receive syn. Then send synback\n",print_index);
                    increase_print_index();


                    Initial_stcp_control_seg(tcb, SYNACK, &server_seg);
                    sip_sendseg(sip_conn,src_node,&server_seg);
                    break;
                case FIN:
                    printf("[%d] | CONNECTED :Server receive fin. Then send finback\n",print_index);
                    increase_print_index();

                    Initial_stcp_control_seg(tcb, FINACK, &server_seg);
                    sip_sendseg(sip_conn,src_node, &server_seg);
                    tcb->state = CLOSEWAIT;

                    //����һ����ʱ�߳�
                    int seghandler_err= pthread_create(fin_tid,
                                                       NULL,close_wait_handler,tcb);
                    assert(seghandler_err==0);
                    pthread_detach(*fin_tid);
                    break;
                case DATA:
                    //����recv�Ĵ�С��ĬĬ����
                    if(seg->header.length+tcb->usedBufLen>RECEIVE_BUF_SIZE)
                    {
                        break;
                    }
                        //������
                    else if(seg->header.seq_num==tcb->expect_seqNum)
                    {
                        //������ص�data�͸���usedlen
                        recvBuf_recv(tcb,seg);

                        //���ͱ���DATAACK����ACKNUM
                        Initial_stcp_control_seg(tcb, DATAACK, &server_seg);
                        sip_sendseg(sip_conn,src_node, &server_seg);
                    }
                    else if(seg->header.seq_num!=tcb->expect_seqNum)
                    {
                        //���ͱ���DATAACK����ACKNUM
                        Initial_stcp_control_seg(tcb, DATAACK, &server_seg);
                        sip_sendseg(sip_conn,src_node, &server_seg);
                    }
                    break;
            }
            break;
        case CLOSEWAIT:
            if (seg->header.type == FIN)
            {
                printf("[%d] | CLOSEWAIT :Server receive fin. Then send finback\n",print_index);
                increase_print_index();

                Initial_stcp_control_seg(tcb, FINACK, &server_seg);
                sip_sendseg(sip_conn,src_node, &server_seg);
            }
            break;
        default:
            break;
    }
}

void *close_wait_handler(void* arg)
{
    server_tcb_t * tcb=(server_tcb_t*)arg;
    sleep(CLOSEWAIT_TIMEOUT);
    tcb->state=CLOSED;

    tcb->usedBufLen=0;//lab4-2

    printf("[%d] | CLOSEWAIT :Close_wait_timeout.Turn to CLOSED!\n",print_index);
    increase_print_index();

    pthread_exit(NULL);
}

//�����µ�tcb��������, ���tcbtable�����е�tcb����ʹ���˻�����Ķ˿ں��ѱ�ʹ��, �ͷ���-1.
int tcbtable_newtcb(unsigned int port)
{
    int i;

    for(i=0;i<MAX_TRANSPORT_CONNECTIONS;i++) {
        if(server_tcbs[i]!=NULL&&server_tcbs[i]->server_portNum==port) {
            return -1;
        }
    }

    for(i=0;i<MAX_TRANSPORT_CONNECTIONS;i++)
    {
        if(server_tcbs[i]==NULL)
        {
            server_tcbs[i] = (server_tcb_t *) malloc(sizeof(server_tcb_t));
            server_tcbs[i]->server_portNum = port;
            return i;
        }
    }
    return -1;
}

//ͨ���������׽��ֻ��tcb.
//���û���ҵ�tcb, ����0.
server_tcb_t * tcbtable_gettcb(int sockfd)
{
    if(server_tcbs[sockfd]!=NULL)
        return server_tcbs[sockfd];
    else
        return 0;
}
//���ܵ���Ӧ�Ķ�֮�󣬸��ݶ��е�һЩ��Ϣ����ȡ��Ӧ�ĵķ�����tcb��
server_tcb_t  *tcbtable_recv_gettcb(seg_t * seg)
{
    int server_tcb_index=-1;
    for(int tcb_index=0;tcb_index<MAX_TRANSPORT_CONNECTIONS;tcb_index++)
    {
        if(seg->header.type==SYN)//������״̬
        {
            if (server_tcbs[tcb_index] != NULL && \
                server_tcbs[tcb_index]->server_portNum == seg->header.dest_port)
            {
                server_tcb_index = tcb_index;
                break;
            }
        }
        else //ȫ����״̬
        {
            if (server_tcbs[tcb_index] != NULL && \
                server_tcbs[tcb_index]->server_portNum == seg->header.dest_port&&\
                server_tcbs[tcb_index]->client_portNum == seg->header.src_port
                    )
            {
                server_tcb_index = tcb_index;
                break;
            }
        }
    }

    if(server_tcb_index<0)
        return NULL;
    server_tcb_t *tcb=server_tcbs[server_tcb_index];
    return tcb;
}
//Defined by Niu in lab4-1 End


//Defined by Niu in lab4-2
void recvBuf_recv(server_tcb_t * tcb,seg_t *seg)
{
    pthread_mutex_lock(tcb->bufMutex);

    //������������
    memcpy(tcb->recvBuf+tcb->usedBufLen,seg->data,seg->header.length);
    //���²���
    tcb->usedBufLen+=seg->header.length;
    tcb->expect_seqNum+=seg->header.length;

    pthread_mutex_unlock(tcb->bufMutex);
}


void recvBuf_copyToClient(server_tcb_t  * tcb,void * buf,unsigned int length)
{
    pthread_mutex_lock(tcb->bufMutex);
    //copy
    memcpy(buf,tcb->recvBuf,sizeof(char)*length);
    //ǰ��
    memcpy(tcb->recvBuf,tcb->recvBuf+length,tcb->usedBufLen-length);
    //���³���
    tcb->usedBufLen=tcb->usedBufLen-length;
    //
    pthread_mutex_unlock(tcb->bufMutex);
}
//Defined by Niu in lab4-2 End

