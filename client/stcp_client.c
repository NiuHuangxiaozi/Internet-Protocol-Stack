#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "stcp_client.h"


//
//  �����������ṩ��ÿ���������õ�ԭ�Ͷ����ϸ��˵��, ����Щֻ��ָ���Ե�, ����ȫ���Ը����Լ����뷨����ƴ���.
//
//  ע��: ��ʵ����Щ����ʱ, ����Ҫ����FSM�����п��ܵ�״̬, �����ʹ��switch�����ʵ��.
//
//  Ŀ��: ������������Ʋ�ʵ������ĺ���ԭ��.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL.  
// ��������ص�����TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, �ñ�����Ϊsip_sendseg��sip_recvseg���������.
// ���, �����������seghandler�߳�����������STCP��. �ͻ���ֻ��һ��seghandler.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

//Defiend by Niu in lab1
#include "assert.h"
#include "unistd.h"
#include "string.h"


client_tcb_t ** client_tcbs=NULL;//����tcb��
pthread_t seghandler_tid;
int Son_sockfd=0;
extern int print_index;  //in order to print information
//Defined by Niu in lab1 End

void stcp_client_init(int conn)
{
    //��ʼ��tcb��
    client_tcbs=(client_tcb_t**)malloc(MAX_TRANSPORT_CONNECTIONS*sizeof(client_tcb_t*));
    for(int tcb_index=0;tcb_index<MAX_TRANSPORT_CONNECTIONS;tcb_index++)
        client_tcbs[tcb_index]=NULL;

    //�����ص�SON���׽���������
    Son_sockfd=conn;

    //����seghandler
    int seghandler_err= pthread_create(&seghandler_tid,NULL,seghandler,NULL);
    pthread_detach(seghandler_tid);
}

// ����һ���ͻ���TCB��Ŀ, �����׽���������
//
// ����������ҿͻ���TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��. ����, TCB state������ΪCLOSED���ͻ��˶˿ڱ�����Ϊ�������ò���client_port.
// TCB������Ŀ��������Ӧ��Ϊ�ͻ��˵����׽���ID�������������, �����ڱ�ʶ�ͻ��˵�����.
// ���TCB����û����Ŀ����, �����������-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_sock(unsigned int client_port) {
    //1��Ѱ���е�TCB��Ŀ��������Ӧ��tcb
    int new_tcb = tcbtable_newtcb(client_port);

    if(new_tcb<0)//ʧ�ܷ���-1
        return -1;
    //2�õ�TCB
    client_tcb_t* my_clienttcb = tcbtable_gettcb(new_tcb);
    //3����tcb������ client��port�Ѿ���tcbtable_newtcb������������

    my_clienttcb->client_nodeID =-1;
    my_clienttcb->server_nodeID = -1;

    my_clienttcb->next_seqNum = 0;
    my_clienttcb->state = CLOSED;

    my_clienttcb->sendBufHead = 0;
    my_clienttcb->sendBufunSent = 0;
    my_clienttcb->sendBufTail = 0;

    my_clienttcb->unAck_segNum = 0;

    //Ϊ���ͻ���������������
    pthread_mutex_t* sendBuf_mutex;
    sendBuf_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    assert(sendBuf_mutex!=NULL);
    pthread_mutex_init(sendBuf_mutex,NULL);
    my_clienttcb->bufMutex = sendBuf_mutex;

    return new_tcb;
}

// ����STCP������
//
// ��������������ӷ�����. �����׽���ID�ͷ������Ķ˿ں���Ϊ�������. �׽���ID�����ҵ�TCB��Ŀ.
// �����������TCB�ķ������˿ں�,  Ȼ��ʹ��sip_sendseg()����һ��SYN�θ�������.
// �ڷ�����SYN��֮��, һ����ʱ��������. �����SYNSEG_TIMEOUTʱ��֮��û���յ�SYNACK, SYN �ν����ش�.
// ����յ���, �ͷ���1. ����, ����ش�SYN�Ĵ�������SYN_MAX_RETRY, �ͽ�stateת����CLOSED, ������-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_connect(int sockfd, unsigned int server_port)
{
    //ͨ��sockfd���tcb
    client_tcb_t* tcb;
    tcb = tcbtable_gettcb(sockfd);
    if(!tcb)return -1;

    //���͵����ݶ�
    seg_t syn;

    //ʣ���ش���Ч����
    int rest_retry=SYN_MAX_RETRY;

    //FSM
    switch (tcb->state) {
            case CLOSED:
                //��ʼ��SYN����
                tcb->server_portNum = server_port;

                Initial_stcp_control_seg(tcb, SYN, &syn);
                //���ͱ���
                sip_sendseg(Son_sockfd, &syn);

                printf("[%d] Client Send Syn :client portNum %d:\n",print_index,tcb->client_portNum);
                increase_print_index();

                //״̬ת��
                tcb->state=SYNSENT;

                //��ʱʱ���³���
                while(rest_retry>0)
                {
                        select(0,0,0,0,&(struct timeval){.tv_usec = SYN_TIMEOUT/1000});
                        if(tcb->state == CONNECTED)
                        {
                                return 1;
                        }
                        else
                        {
                            sip_sendseg(Son_sockfd, &syn);
                            rest_retry--;
                        }
                }
                //״̬ת��
                tcb->state = CLOSED;
                return -1;
                break;
            case SYNSENT:
                return -1;
                break;
            case CONNECTED:
                return -1;
                break;
            case FINWAIT:
                return -1;
                break;
            default:
                return -1;
                break;
    }
}

// �������ݸ�STCP������
int stcp_client_send(int sockfd, void* data, unsigned int length)
{
    //ͨ��sockfd���tcb
    client_tcb_t* clienttcb;
    clienttcb = tcbtable_gettcb(sockfd);
    if(!clienttcb)
        return -1;

    int segNum;
    int i;
    switch(clienttcb->state)
    {
        case CLOSED:
            return -1;
        case SYNSENT:
            return -1;
        case CONNECTED:
            //ʹ���ṩ�����ݴ�����
            segNum = length/MAX_SEG_LEN;
            if(length%MAX_SEG_LEN)
                segNum++;
            for(i=0;i<segNum;i++)
            {
                segBuf_t* newBuf = (segBuf_t*) malloc(sizeof(segBuf_t));
                assert(newBuf!=NULL);
                bzero(newBuf,sizeof(segBuf_t));
                newBuf->seg.header.src_port = clienttcb->client_portNum;
                newBuf->seg.header.dest_port = clienttcb->server_portNum;
                if(length%MAX_SEG_LEN!=0 && i==segNum-1)
                    newBuf->seg.header.length = length%MAX_SEG_LEN;
                else
                    newBuf->seg.header.length = MAX_SEG_LEN;
                newBuf->seg.header.type = DATA;
                char* datatosend = (char*)data;
                memcpy(newBuf->seg.data,&datatosend[i*MAX_SEG_LEN],newBuf->seg.header.length);
                sendBuf_addSeg(clienttcb,newBuf);
            }

            //���Ͷ�ֱ��δȷ�϶���Ŀ�ﵽGBN_WINDOWΪֹ, ���sendBuf_timerû������, ��������.
            sendBuf_send(clienttcb);
            return 1;
        case FINWAIT:
            return -1;
        default:
            return -1;
    }
}

// �Ͽ���STCP������������
//
// ����������ڶϿ���������������. �����׽���ID��Ϊ�������. �׽���ID�����ҵ�TCB���е���Ŀ.
// �����������FIN segment��������. �ڷ���FIN֮��, state��ת����FINWAIT, ������һ����ʱ��.
// ��������ճ�ʱ֮ǰstateת����CLOSED, �����FINACK�ѱ��ɹ�����. ����, ����ھ���FIN_MAX_RETRY�γ���֮��,
// state��ȻΪFINWAIT, state��ת����CLOSED, ������-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_disconnect(int sockfd)
{
    //�ҵ�tcb����Ŀ
    //ͨ��sockfd���tcb
    client_tcb_t* tcb;
    tcb = tcbtable_gettcb(sockfd);
    if(!tcb)return -1;

    //���͵����ݶ�
    seg_t fin;
    //FSM
    while(1) {
        switch (tcb->state)
        {
            case CLOSED:
                return -1;
                break;
            case SYNSENT:
                return -1;
                break;
            case CONNECTED:
                //��ʼFIN����
                bzero(&fin,sizeof(fin));
                Initial_stcp_control_seg(tcb, FIN, &fin);

                //����FIN
                sip_sendseg(Son_sockfd, &fin);

                printf("[%d] | Client send fin :client portNum %d:\n",print_index,tcb->client_portNum);
                increase_print_index();
                //״̬ת��
                tcb->state = FINWAIT;
                printf("[%d] | CLIENT: FINWAIT\n",print_index);

                //�����ʱ���ط�
                int retry = FIN_MAX_RETRY;
                while(retry>0) {
                    select(0,0,0,0,&(struct timeval){.tv_usec = FIN_TIMEOUT/1000});
                    if(tcb->state == CLOSED) {
                        tcb->server_nodeID = -1;
                        tcb->server_portNum = 0;
                        tcb->next_seqNum = 0;
                        sendBuf_clear(tcb);
                        return 1;
                    }
                    else {
                        printf("[%d] | CLIENT: FIN RESENT\n",print_index);
                        sip_sendseg(Son_sockfd, &fin);
                        retry--;
                    }
                }
                tcb->state = CLOSED;
                return -1;
                break;
            case FINWAIT:
                return -1;
                break;
            default:
                return -1;
                break;
        }
    }
    return 0;
}

// �ر�STCP�ͻ�
//
// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_close(int sockfd)
{
    //ͨ��sockfd���tcb
    client_tcb_t* clienttcb;
    clienttcb = tcbtable_gettcb(sockfd);
    if(!clienttcb)
        return -1;

    switch(clienttcb->state) {
        case CLOSED:
            free(clienttcb->bufMutex);
            free(client_tcbs[sockfd]);
            client_tcbs[sockfd]=NULL;
            return 1;
        case SYNSENT:
            return -1;
        case CONNECTED:
            return -1;
        case FINWAIT:
            return -1;
        default:
            return -1;
    }
}

// �������ε��߳�
//
// ������stcp_client_init()�������߳�. �������������Է������Ľ����.
// seghandler�����Ϊһ������sip_recvseg()������ѭ��. ���sip_recvseg()ʧ��, ��˵���ص����������ѹر�,
// �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���. ��鿴�ͻ���FSM���˽����ϸ��.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

void *seghandler(void* arg)
{
    seg_t  recv_seg;
    while(1)
    {
        int recv_flag=sip_recvseg(Son_sockfd,&recv_seg);
        if(recv_flag==-1)//�ص������Ͽ�
        {
            printf("[%d] | SON disconnected seghandler exit!\n",print_index);
            increase_print_index();
            break;
        }
        else if(recv_flag==0)//������
        {
            //do nothing
            printf("[%d] | client missing package\n",print_index);
            increase_print_index();
        }
        else if(recv_flag==1)//checksum�������
        {
            printf("[%d] | The package checksum error!\n",print_index);
            increase_print_index();
        }
        else if(recv_flag==2)//�յ��˰�
        {
            printf("[%d] | client received package\n",print_index);
            increase_print_index();

            action(&recv_seg);
        }
    }
    pthread_exit(NULL);
    return 0;
}






//Defined by Niu in lab4-1

//��tcbtable�л��һ���µ�tcb, ʹ�ø����Ķ˿ںŷ�����ͻ��˶˿�.
//�����µ�tcb��������, ���tcbtable�����е�tcb����ʹ���˻�����Ķ˿ں��ѱ�ʹ��, �ͷ���-1.
int tcbtable_newtcb(unsigned int port) {
    int i;

    for(i=0;i<MAX_TRANSPORT_CONNECTIONS;i++) {
        if(client_tcbs[i]!=NULL&&client_tcbs[i]->client_portNum==port) {
            return -1;
        }
    }

    for(i=0;i<MAX_TRANSPORT_CONNECTIONS;i++) {
        if(client_tcbs[i]==NULL) {
            client_tcbs[i] = (client_tcb_t*) malloc(sizeof(client_tcb_t));
            client_tcbs[i]->client_portNum = port;
            return i;
        }
    }
    return -1;
}

//ͨ���������׽��ֻ��tcb.
//���û���ҵ�tcb, ����0.
client_tcb_t* tcbtable_gettcb(int sockfd)
{
    if(client_tcbs[sockfd]!=NULL)
        return client_tcbs[sockfd];
    else
        return 0;
}

void Initial_stcp_control_seg(client_tcb_t * tcb,int Signal,seg_t  *syn)
{
    //����
    bzero(syn,sizeof(*syn));
    //��������������Ϣ
    syn->header.src_port=tcb->client_portNum; //Դ�˿�
    syn->header.dest_port=tcb->server_portNum;//Ŀ��Ķ˿ں�
    //syn->header.seq_num=0;        //���
    //syn->header.ack_num=0;       //ȷ�Ϻ�

    switch(Signal) { //type
        case SYN:
            syn->header.type = SYN;
            syn->header.length=0;
            syn->header.seq_num=0;
            break;
        case SYNACK:
            break;
        case FIN:
            syn->header.type = FIN;
            syn->header.length=0;
            break;
        case FINACK:
            break;
        case DATA:
            break;
        case DATAACK:
            break;
        default:
            break;
    }
}

void action(seg_t  *segBuf)
{
    //�ҵ�tcb�������
    client_tcb_t* my_clienttcb = tcbtable_gettcbFromPort(segBuf->header.dest_port);
    if(!my_clienttcb)
    {
        printf("CLIENT: NO PORT FOR RECEIVED SEGMENT\n");
        return;
    }
    //�δ���
    switch(my_clienttcb->state) {
        case CLOSED:
            break;
        case SYNSENT:
            //&&my_clienttcb->server_nodeID==src_nodeID
            if(segBuf->header.type==SYNACK&&my_clienttcb->server_portNum==segBuf->header.src_port)
            {
                printf("CLIENT: SYNACK RECEIVED\n");
                my_clienttcb->state = CONNECTED;
                printf("CLIENT: CONNECTED\n");
            }
            else
                printf("CLIENT: IN SYNSENT, NON SYNACK SEG RECEIVED\n");
            break;
        case CONNECTED:
            //&&my_clienttcb->server_nodeID==src_nodeID
            if(segBuf->header.type==DATAACK&&\
               my_clienttcb->server_portNum==segBuf->header.src_port)
               {
                    if(my_clienttcb->sendBufHead!=NULL&&
                    segBuf->header.ack_num >= my_clienttcb->sendBufHead->seg.header.seq_num)
                    {
                        //���յ�ack, ���·��ͻ�����
                        sendBuf_recvAck(my_clienttcb, segBuf->header.ack_num);
                        //�����ڷ��ͻ������е��¶�
                        sendBuf_send(my_clienttcb);
                    }
                }
            else {
                printf("CLIENT: IN CONNECTED, NON DATAACK SEG RECEIVED\n");
            }
            break;
        case FINWAIT:
            //&&my_clienttcb->server_nodeID==src_nodeID
            if(segBuf->header.type==FINACK&&
               my_clienttcb->server_portNum==segBuf->header.src_port)
            {
                printf("CLIENT: FINACK RECEIVED\n");
                my_clienttcb->state = CLOSED;
                printf("CLIENT: CLOSED\n");
            }
            else
                printf("CLIENT: IN FINWAIT, NON FINACK SEG RECEIVED\n");
            break;
    }
}




//ͨ�������Ŀͻ��˶˿ںŻ��tcb.
//���û���ҵ�tcb, ����0.
client_tcb_t* tcbtable_gettcbFromPort(unsigned int clientPort)
{
    int i;
    for(i=0;i<MAX_TRANSPORT_CONNECTIONS;i++) {
        if(client_tcbs[i]!=NULL && client_tcbs[i]->client_portNum==clientPort) {
            return client_tcbs[i];
        }
    }
    return 0;
}
//funcs Defined by niu in lab 4-1 End


//funcs Defined by niu in lab 4-2
/*******************************************************/
//
// ���ڷ��ͻ����������ĸ�������
//
/*******************************************************/

//����̳߳�����ѯ���ͻ������Դ�����ʱ�¼�. ������ͻ������ǿ�, ��Ӧһֱ����.
//���(��ǰʱ�� - ��һ���ѷ��͵�δ��ȷ�϶εķ���ʱ��) > DATA_TIMEOUT, �͵���sendBuf_timeout().
void* sendBuf_timer(void* clienttcb)
{
    client_tcb_t* my_clienttcb = (client_tcb_t*) clienttcb;
    while(1)
    {
        select(0,0,0,0,&(struct timeval){.tv_usec = SENDBUF_POLLING_INTERVAL/1000});

        //������ڵ�ʱ��
        struct timeval currentTime;
        gettimeofday(&currentTime,NULL);

        //���unAck_segNumΪ0, ��ζ�ŷ��ͻ�������û�ж�, �˳�.
        if(my_clienttcb->unAck_segNum == 0) {
            pthread_exit(NULL);
        }
        else if(my_clienttcb->sendBufHead->sentTime>0 && my_clienttcb->sendBufHead->sentTime<currentTime.tv_sec*1000000+currentTime.tv_usec-DATA_TIMEOUT) {
            sendBuf_timeout(my_clienttcb);
        }
    }
}
//����ʱ�¼�����ʱ, �������������.
//���·�����clienttcb�ķ��ͻ������������ѷ��͵�δ��ȷ�϶�.
void sendBuf_timeout(client_tcb_t* clienttcb)
{
    pthread_mutex_lock(clienttcb->bufMutex);
    segBuf_t* bufPtr=clienttcb->sendBufHead;
    int i;
    for(i=0;i<clienttcb->unAck_segNum;i++)
    {
        printf("sendBuf timeout | \n");
        print_seg(&(bufPtr->seg));
        sip_sendseg(Son_sockfd, &(bufPtr->seg));
        struct timeval currentTime;
        gettimeofday(&currentTime,NULL);
        bufPtr->sentTime = currentTime.tv_sec*1000000+ currentTime.tv_usec;
        bufPtr = bufPtr->next;
    }
    pthread_mutex_unlock(clienttcb->bufMutex);
}

//���һ���ε����ͻ�������
//��Ӧ�ó�ʼ��segBuf�б�Ҫ���ֶ�, Ȼ��segBuf��ӵ�clienttcbʹ�õķ��ͻ�����������.
void sendBuf_addSeg(client_tcb_t* clienttcb, segBuf_t* newSegBuf)
{
    pthread_mutex_lock(clienttcb->bufMutex);
    if(clienttcb->sendBufHead==0) {
        newSegBuf->seg.header.seq_num = clienttcb->next_seqNum;
        clienttcb->next_seqNum += newSegBuf->seg.header.length;
        newSegBuf->sentTime = 0;
        clienttcb->sendBufHead= newSegBuf;
        clienttcb->sendBufunSent = newSegBuf;
        clienttcb->sendBufTail = newSegBuf;
    } else {
        newSegBuf->seg.header.seq_num = clienttcb->next_seqNum;
        clienttcb->next_seqNum += newSegBuf->seg.header.length;
        newSegBuf->sentTime = 0;
        clienttcb->sendBufTail->next = newSegBuf;
        clienttcb->sendBufTail = newSegBuf;
        if(clienttcb->sendBufunSent == 0)
            clienttcb->sendBufunSent = newSegBuf;
    }
    pthread_mutex_unlock(clienttcb->bufMutex);
}

//����clienttcb�ķ��ͻ������еĶ�, ֱ���ѷ��͵�δ��ȷ�϶ε���������GBN_WINDOWΪֹ.
//�����Ҫ, ������sendBuf_timer.
void sendBuf_send(client_tcb_t* clienttcb)
{
    pthread_mutex_lock(clienttcb->bufMutex);

    while(clienttcb->unAck_segNum<GBN_WINDOW && clienttcb->sendBufunSent!=0) {
        sip_sendseg(Son_sockfd, (seg_t*)clienttcb->sendBufunSent);
        struct timeval currentTime;
        gettimeofday(&currentTime,NULL);
        clienttcb->sendBufunSent->sentTime = currentTime.tv_sec*1000000+ currentTime.tv_usec;
        //�ڷ����˵�һ�����ݶ�֮��Ӧ����sendBuf_timer
        if(clienttcb->unAck_segNum ==0) {
            pthread_t timer;
            pthread_create(&timer,NULL,sendBuf_timer, (void*)clienttcb);
        }
        clienttcb->unAck_segNum++;

        if(clienttcb->sendBufunSent != clienttcb->sendBufTail)
            clienttcb->sendBufunSent= clienttcb->sendBufunSent->next;
        else
            clienttcb->sendBufunSent = 0;
    }
    pthread_mutex_unlock(clienttcb->bufMutex);
}

//��clienttcbת����CLOSED״̬ʱ, �������������.
//��ɾ��clienttcb�ķ��ͻ���������, ������ͻ�����ָ��.
void sendBuf_clear(client_tcb_t* clienttcb)
{
    pthread_mutex_lock(clienttcb->bufMutex);
    segBuf_t* bufPtr = clienttcb->sendBufHead;
    while(bufPtr!=clienttcb->sendBufTail) {
        segBuf_t* temp = bufPtr;
        bufPtr = bufPtr->next;
        free(temp);
    }
    free(clienttcb->sendBufTail);
    clienttcb->sendBufunSent = 0;
    clienttcb->sendBufHead = 0;
    clienttcb->sendBufTail = 0;
    clienttcb->unAck_segNum = 0;
    pthread_mutex_unlock(clienttcb->bufMutex);
}


//�����յ�һ��DATAACKʱ, �������������.
//��Ӧ�ø���clienttcb�е�ָ��, �ͷ�������ȷ��segBuf.
void sendBuf_recvAck(client_tcb_t* clienttcb, unsigned int ack_seqnum)
{
    pthread_mutex_lock(clienttcb->bufMutex);

    //������жζ���ȷ����
    if(ack_seqnum>clienttcb->sendBufTail->seg.header.seq_num)
        clienttcb->sendBufTail = 0;

    segBuf_t* bufPtr= clienttcb->sendBufHead;
    while(bufPtr && bufPtr->seg.header.seq_num<ack_seqnum) {
        clienttcb->sendBufHead = bufPtr->next;
        segBuf_t* temp = bufPtr;
        bufPtr = bufPtr->next;
        free(temp);
        clienttcb->unAck_segNum--;
    }
    pthread_mutex_unlock(clienttcb->bufMutex);
}

//Defined by Niu in lab4-2 End