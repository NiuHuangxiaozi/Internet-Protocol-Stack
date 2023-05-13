// �ļ��� pkt.c

#include "pkt.h"

//defined in lab4-4
#include "stdlib.h"
#include <sys/types.h>
#include <sys/socket.h>
#include "assert.h"
#include "string.h"
//defined in lab4-4 end
// son_sendpkt()��SIP���̵���, ��������Ҫ��SON���̽����ķ��͵��ص�������. SON���̺�SIP����ͨ��һ������TCP���ӻ���.
// ��son_sendpkt()��, ���ļ�����һ���Ľڵ�ID����װ�����ݽṹsendpkt_arg_t, ��ͨ��TCP���ӷ��͸�SON����. 
// ����son_conn��SIP���̺�SON����֮���TCP�����׽���������.
// ��ͨ��SIP���̺�SON����֮���TCP���ӷ������ݽṹsendpkt_arg_tʱ, ʹ��'!&'��'!#'��Ϊ�ָ���, ����'!& sendpkt_arg_t�ṹ !#'��˳����.
// ������ͳɹ�, ����1, ���򷵻�-1.
int son_sendpkt(int nextNodeID, sip_pkt_t* pkt, int son_conn)
{
    //׼������
    sendpkt_arg_t sip2son;
    sip2son.nextNodeID=nextNodeID;
    memcpy((&sip2son.pkt), pkt, sizeof((*pkt)));
    //���ͱ���
    //����send
    char start_symbol[2]={'!','&'};
    ssize_t begin_err = send(son_conn,start_symbol, sizeof(start_symbol), 0);
    assert(begin_err > 0);
    if(begin_err<=0) return -1;

    size_t sip_pkt_err = send(son_conn,&sip2son,sizeof(sip2son), 0);
    assert(sip_pkt_err > 0);
    if(sip_pkt_err<=0) return -1;

    char end_symbol[2]={'!','#'};
    ssize_t end_err = send(son_conn,end_symbol, sizeof(end_symbol), 0);
    assert(end_err > 0);
    if(end_err<=0)return -1;

    return 1;
}

// son_recvpkt()������SIP���̵���, �������ǽ�������SON���̵ı���. 
// ����son_conn��SIP���̺�SON����֮��TCP���ӵ��׽���������. ����ͨ��SIP���̺�SON����֮���TCP���ӷ���, ʹ�÷ָ���!&��!#. 
// Ϊ�˽��ձ���, �������ʹ��һ���򵥵�����״̬��FSM
// PKTSTART1 -- ��� 
// PKTSTART2 -- ���յ�'!', �ڴ�'&' 
// PKTRECV -- ���յ�'&', ��ʼ��������
// PKTSTOP1 -- ���յ�'!', �ڴ�'#'�Խ������ݵĽ��� 
// ����ɹ����ձ���, ����1, ���򷵻�-1.
int son_recvpkt(sip_pkt_t* pkt, int son_conn)
{
    //�����״̬
    int cur_state=SEGSTART1;
    //��ǰ���ܵ��ַ�
    char symbol;
    //�ж��Ƿ��Ѿ����ܵ���
    int is_received=0;

    //��ʱ�Ļ�����
    int buf_size=sizeof(*pkt);
    char temp_buf[buf_size];
    int temp_buf_loc=0;//��ʱ���������±�ָʾ

    //���ܶε�FSM
    while(1)
    {
        ssize_t recv_flag=recv(son_conn,&symbol,sizeof(symbol),0);
        //�ص������Ͽ���
        if(recv_flag==0)return -1;
        switch (cur_state)
        {
            case SEGSTART1:
                if (symbol == '!')
                    cur_state = SEGSTART2;
                break;
            case SEGSTART2:
                if (symbol == '&')
                    cur_state = SEGRECV;
                else
                    cur_state = SEGSTART1;
                break;
            case SEGRECV:
                if(symbol == '!')
                    cur_state = SEGSTOP1;
                else
                {
                    memcpy((char *) (temp_buf + temp_buf_loc), &symbol, 1);
                    temp_buf_loc++;
                }
                break;
            case SEGSTOP1:
                if (symbol == '#')
                {
                    is_received=1;
                }
                else
                {
                    char pre_sym='!';
                    memcpy((char *) (temp_buf + temp_buf_loc), &pre_sym, 1);
                    temp_buf_loc++;
                    memcpy((char *) (temp_buf + temp_buf_loc), &symbol, 1);
                    temp_buf_loc++;
                    cur_state = SEGRECV;
                }
                break;
            default:
                break;
        }
        if(is_received==1)
            break;
    }
    //��seg�ӻ�������������һ����
    memcpy(pkt,temp_buf,temp_buf_loc);
    return 1;
}

// ���������SON���̵���, �������ǽ������ݽṹsendpkt_arg_t.
// ���ĺ���һ���Ľڵ�ID����װ��sendpkt_arg_t�ṹ.
// ����sip_conn����SIP���̺�SON����֮���TCP���ӵ��׽���������. 
// sendpkt_arg_t�ṹͨ��SIP���̺�SON����֮���TCP���ӷ���, ʹ�÷ָ���!&��!#. 
// Ϊ�˽��ձ���, �������ʹ��һ���򵥵�����״̬��FSM
// PKTSTART1 -- ��� 
// PKTSTART2 -- ���յ�'!', �ڴ�'&' 
// PKTRECV -- ���յ�'&', ��ʼ��������
// PKTSTOP1 -- ���յ�'!', �ڴ�'#'�Խ������ݵĽ���
// ����ɹ�����sendpkt_arg_t�ṹ, ����1, ���򷵻�-1.
int getpktToSend(sip_pkt_t* pkt, int* nextNode,int sip_conn)
{
    //�����״̬
    int cur_state=SEGSTART1;
    //��ǰ���ܵ��ַ�
    char symbol;
    //�ж��Ƿ��Ѿ����ܵ���
    int is_received=0;

    //��ʱ�Ļ�����
    sendpkt_arg_t temp_pkt;
    int buf_size=sizeof(temp_pkt);
    char temp_buf[buf_size];
    int temp_buf_loc=0;//��ʱ���������±�ָʾ

    //���ܶε�FSM
    while(1)
    {
        ssize_t recv_flag=recv(sip_conn,&symbol,sizeof(symbol),0);
        //�ص������Ͽ���
        if(recv_flag==0)return -1;
        switch (cur_state)
        {
            case SEGSTART1:
                if (symbol == '!')
                    cur_state = SEGSTART2;
                break;
            case SEGSTART2:
                if (symbol == '&')
                    cur_state = SEGRECV;
                else
                    cur_state = SEGSTART1;
                break;
            case SEGRECV:
                if(symbol == '!')
                    cur_state = SEGSTOP1;
                else
                {
                    memcpy((char *) (temp_buf + temp_buf_loc), &symbol, 1);
                    temp_buf_loc++;
                }
                break;
            case SEGSTOP1:
                if (symbol == '#')
                {
                    is_received=1;
                }
                else
                {
                    char pre_sym='!';
                    memcpy((char *) (temp_buf + temp_buf_loc), &pre_sym, 1);
                    temp_buf_loc++;
                    memcpy((char *) (temp_buf + temp_buf_loc), &symbol, 1);
                    temp_buf_loc++;
                    cur_state = SEGRECV;
                }
                break;
            default:
                break;
        }
        if(is_received==1)
            break;
    }
    //��seg�ӻ�����������sendpkt��
    memcpy(&temp_pkt,temp_buf,temp_buf_loc);
    //�����sip����
    memcpy(pkt,&(temp_pkt.pkt),sizeof(temp_pkt.pkt));
    //�����Ҫ���͵Ľڵ�
    (*nextNode)=temp_pkt.nextNodeID;
    return 1;
}

// forwardpktToSIP()��������SON���̽��յ������ص����������ھӵı��ĺ󱻵��õ�. 
// SON���̵����������������ת����SIP����. 
// ����sip_conn��SIP���̺�SON����֮���TCP���ӵ��׽���������. 
// ����ͨ��SIP���̺�SON����֮���TCP���ӷ���, ʹ�÷ָ���!&��!#, ����'!& ���� !#'��˳����. 
// ������ķ��ͳɹ�, ����1, ���򷵻�-1.
int forwardpktToSIP(sip_pkt_t* pkt, int sip_conn)
{
    //���ͱ���
    //����send
    char start_symbol[2]={'!','&'};
    ssize_t begin_err = send(sip_conn,start_symbol, sizeof(start_symbol), 0);
    assert(begin_err > 0);
    if(begin_err<=0) return -1;

    size_t sip_pkt_err = send(sip_conn,pkt,sizeof(*pkt), 0);
    assert(sip_pkt_err > 0);
    if(sip_pkt_err<=0) return -1;

    char end_symbol[2]={'!','#'};
    ssize_t end_err = send(sip_conn,end_symbol, sizeof(end_symbol), 0);
    assert(end_err > 0);
    if(end_err<=0)return -1;
    return 1;
}

// sendpkt()������SON���̵���, �������ǽ�������SIP���̵ı��ķ��͸���һ��.
// ����conn�ǵ���һ���ڵ��TCP���ӵ��׽���������.
// ����ͨ��SON���̺����ھӽڵ�֮���TCP���ӷ���, ʹ�÷ָ���!&��!#, ����'!& ���� !#'��˳����. 
// ������ķ��ͳɹ�, ����1, ���򷵻�-1.
int sendpkt(sip_pkt_t* pkt, int conn)
{
    //���ͱ���
    //����send
    char start_symbol[2]={'!','&'};
    ssize_t begin_err = send(conn,start_symbol, sizeof(start_symbol), 0);
    assert(begin_err > 0);
    if(begin_err<=0) return -1;

    size_t sip_pkt_err = send(conn,pkt,sizeof(*pkt), 0);
    assert(sip_pkt_err > 0);
    if(sip_pkt_err<=0) return -1;

    char end_symbol[2]={'!','#'};
    ssize_t end_err = send(conn,end_symbol, sizeof(end_symbol), 0);
    assert(end_err > 0);
    if(end_err<=0)return -1;
    return 1;
}

// recvpkt()������SON���̵���, �������ǽ��������ص����������ھӵı���.
// ����conn�ǵ����ھӵ�TCP���ӵ��׽���������.
// ����ͨ��SON���̺����ھ�֮���TCP���ӷ���, ʹ�÷ָ���!&��!#. 
// Ϊ�˽��ձ���, �������ʹ��һ���򵥵�����״̬��FSM
// PKTSTART1 -- ��� 
// PKTSTART2 -- ���յ�'!', �ڴ�'&' 
// PKTRECV -- ���յ�'&', ��ʼ��������
// PKTSTOP1 -- ���յ�'!', �ڴ�'#'�Խ������ݵĽ��� 
// ����ɹ����ձ���, ����1, ���򷵻�-1.
int recvpkt(sip_pkt_t* pkt, int conn)
{
    //�����״̬
    int cur_state=SEGSTART1;
    //��ǰ���ܵ��ַ�
    char symbol;
    //�ж��Ƿ��Ѿ����ܵ���
    int is_received=0;

    //��ʱ�Ļ�����
    int buf_size=sizeof(*pkt);
    char temp_buf[buf_size];
    int temp_buf_loc=0;//��ʱ���������±�ָʾ

    //���ܶε�FSM
    while(1)
    {
        ssize_t recv_flag=recv(conn,&symbol,sizeof(symbol),0);
        //�ص������Ͽ���
        if(recv_flag==0)return -1;
        switch (cur_state)
        {
            case SEGSTART1:
                if (symbol == '!')
                    cur_state = SEGSTART2;
                break;
            case SEGSTART2:
                if (symbol == '&')
                    cur_state = SEGRECV;
                else
                    cur_state = SEGSTART1;
                break;
            case SEGRECV:
                if(symbol == '!')
                    cur_state = SEGSTOP1;
                else
                {
                    memcpy((char *) (temp_buf + temp_buf_loc), &symbol, 1);
                    temp_buf_loc++;
                }
                break;
            case SEGSTOP1:
                if (symbol == '#')
                {
                    is_received=1;
                }
                else
                {
                    char pre_sym='!';
                    memcpy((char *) (temp_buf + temp_buf_loc), &pre_sym, 1);
                    temp_buf_loc++;
                    memcpy((char *) (temp_buf + temp_buf_loc), &symbol, 1);
                    temp_buf_loc++;
                    cur_state = SEGRECV;
                }
                break;
            default:
                break;
        }
        if(is_received==1)
            break;
    }
    //��seg�ӻ�������������һ����
    memcpy(pkt,temp_buf,temp_buf_loc);
    return 1;
}
