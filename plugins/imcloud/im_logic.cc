

#include "im_logic.h"
#include "logic/swp_infos.h"
#include "comm/comm_head.h"
#include "config/config.h"
#include "core/common.h"
#include "logic/logic_comm.h"
#include "logic/logic_unit.h"
#include "net/errno.h"
#include <string>
#include "operator_code.h"
#include "im_proto.h"
#include "im_process.h"

#define DEFAULT_CONFIG_PATH "./plugins/imcloud/im_config.xml"

#define TIME_DISTRIBUTION_TASK 10001

namespace im_logic {

Imlogic *Imlogic::instance_ = NULL;

Imlogic::Imlogic() {
}

Imlogic::~Imlogic() {

}

bool Imlogic::Init() {
  return true;
}

Imlogic *Imlogic::GetInstance() {
  if (instance_ == NULL)
    instance_ = new Imlogic();
  return instance_;
}

void Imlogic::FreeInstance() {
  delete instance_;
  instance_ = NULL;
}

bool Imlogic::OnImConnect(struct server *srv, const int socket) {
  std::string ip;
  int port;
  logic::SomeUtils::GetIPAddress(socket, ip, port);
  LOG_MSG2("ip {%s} prot {%d}", ip.c_str(), port);
  return true;
}

bool Imlogic::OnImMessage(struct server *srv, const int socket,
                                    const void *msg, const int len) {                               
  bool r = false;
  struct PacketHead *packet = NULL;
  if (srv == NULL || socket < 0 || msg == NULL || len < PACKET_HEAD_LENGTH)
    return false;
  if (!net::PacketProsess::UnpackStream(msg, len, &packet)) {
    LOG_ERROR2("UnpackStream Error socket %d", socket);
    send_error(socket, ERROR_TYPE, ERROR_TYPE, FORMAT_ERRNO);
    return false;
  }
  
  //OnGetTokenImcloud(srv, socket, packet);
  switch (packet->operate_code) {
    case R_IMCLOUD_GETTOKEN: {
      OnGetTokenImcloud(srv, socket, packet);
      break;
    }
    case R_IMCLOUD_REFRESHTOKEN: {
      OnRefreshTokenImcloud(srv, socket, packet);
      break;
    }
    case R_IMCLOUD_LOGIN:{
      OnLoginImcloud(srv, socket, packet);
      break;
    }
    default:
      break;
  }

  return true;
}
bool Imlogic::OnGetTokenImcloud(struct server* srv,int socket ,struct PacketHead* packet){
  LOG_ERROR("========================OnGetTokenImcloud============================");
  if (packet->packet_length <= PACKET_HEAD_LENGTH) {
    send_error(socket, ERROR_TYPE, FORMAT_ERRNO, packet->session_id);
    return false;
  }
  im_logic::net_request::tokencode tokencode;
  struct PacketControl* packet_recv = (struct PacketControl*) (packet);
  tokencode.set_http_packet(packet_recv->body_);

  LOG_MSG2("getmessage------------------------:%s,%s",tokencode.name().c_str(),tokencode.accid().c_str());
  /*
  int length = packet->packet_length - sizeof(PacketHead);
  char message[length+1];
  strcpy(message,(char*)(packet+sizeof(PacketHead)));
  LOG_MSG2("get message = %s \n",message);
  */
  im_process::ImProcess tokenfun;
  std::string tokenvalue = tokenfun.gettoken(tokencode.name(),tokencode.accid());
  if(sizeof(tokenvalue)<=0){
	  send_error(socket, ERROR_TYPE, FORMAT_ERRNO, packet->session_id);
	  return false;
  }
	  
  //构建回复包
  im_logic::net_reply::tokenreply reply;
  reply.set_token(tokenvalue);
  std::string code = "success";
  reply.set_result(code);
  struct PacketControl packet_reply;
  MAKE_HEAD(packet_reply, S_IMCLOUD_GETTOKEN, IM_TYPE, 0,packet->session_id, 0);
  packet_reply.body_ = reply.get();
  send_message(socket, &packet_reply);
  
  return true;
}
bool Imlogic::OnRefreshTokenImcloud(struct server* srv,int socket ,struct PacketHead* packet){
  if (packet->packet_length <= PACKET_HEAD_LENGTH) {
    send_error(socket, ERROR_TYPE, FORMAT_ERRNO, packet->session_id);
    return false;
  }
  im_logic::net_request::tokencode tokencode;
  struct PacketControl* packet_recv = (struct PacketControl*) (packet);
  tokencode.set_http_packet(packet_recv->body_);

  im_process::ImProcess tokenfun;
  std::string tokenvalue = tokenfun.refreshtoken(tokencode.accid());
  if(sizeof(tokenvalue)<=0){
	  send_error(socket, ERROR_TYPE, FORMAT_ERRNO, packet->session_id);
	  return false;
  }
	  
  //构建回复包
  im_logic::net_reply::tokenreply reply;
  reply.set_token(tokenvalue);
  std::string code = "success";
  reply.set_result(code);
  struct PacketControl packet_reply;
  MAKE_HEAD(packet_reply, S_IMCLOUD_REFRESHTOKEN, IM_TYPE, 0,packet->session_id, 0);
  packet_reply.body_ = reply.get();
  send_message(socket, &packet_reply);
  
  return true;
}
bool Imlogic::OnRegisterImcloud(struct server* srv,int socket ,struct PacketHead* packet){
  return true;
}
bool Imlogic::OnLoginImcloud(struct server* srv,int socket ,struct PacketHead* packet){
  return true;
}

bool Imlogic::OnImClose(struct server *srv, const int socket) {
  return true;
}

bool Imlogic::OnBroadcastConnect(struct server *srv, const int socket,
                                      const void *msg, const int len) {
  return true;
}

bool Imlogic::OnBroadcastMessage(struct server *srv, const int socket,
                                      const void *msg, const int len) {

  return true;
}

bool Imlogic::OnBroadcastClose(struct server *srv, const int socket) {
  return true;
}

bool Imlogic::OnIniTimer(struct server *srv) {
  if (srv->add_time_task != NULL) {
    if (srv->add_time_task != NULL) {
      srv->add_time_task(srv, "im", TIME_DISTRIBUTION_TASK, 3, -1);
    }
  }
  return true;
}

bool Imlogic::OnTimeout(struct server *srv, char *id, int opcode,
                             int time) {
  switch (opcode) {
    default:
      break;
  }
  return true;
}
}  // namespace
