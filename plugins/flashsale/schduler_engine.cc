//  Copyright (c) 2016-2017 The star Authors. All rights reserved.
//  Created on: 2017年1月7日 Author: kerry

#include "flashsale/schduler_engine.h"
#include "basic/template.h"
#include "net/comm_head.h"
#include "net/packet_processing.h"
#include "logic/logic_unit.h"
#include "logic/time.h"
#include "flashsale/errno.h"
#include "flashsale/operator_code.h"


namespace flash_logic {
FlashManager* FlashEngine::schduler_mgr_ = NULL;
FlashEngine* FlashEngine::schduler_engine_ = NULL;

FlashManager::FlashManager() {
    InitThreadrw(&lock_);
    Init();
}

FlashManager::~FlashManager() {
    DeinitThreadrw(lock_);
    if(!share_memory_){
      delete share_memory_;
      share_memory_ = NULL;
    }
}

void FlashManager::InitDB(flash_logic::FlashDB* flash_db) {
    flash_db_ = flash_db;
}

void FlashManager::InitKafka(flash_logic::FlashKafka* flash_kafka) {
    flash_kafka_ = flash_kafka;
}

void FlashManager::Init() {
    
}

void FlashManager::InitManagerSchduler(manager_schduler::SchdulerEngine* schduler_engine) {
  manager_schduler_engine_ = schduler_engine;
}

void FlashManager::InitData() {
  share_memory_ = new CShareMemory();
  if(share_memory_ == NULL)
    LOG_MSG("Create share memory failure.");
  flash_db_->OnFetchPublishStar(publish_star_map_);
  LOG_MSG2("flash map %lld", publish_star_map_.size());
  if(!share_memory_->AttachMemry(0)){
    LOG_MSG("Attach memory failure.");
    return ;
  }
  share_memory_->SetStarToShareMemory(publish_star_map_);
  CreateTimeTask();
}

void FlashManager::CreateTimeTask() {

    PUBLISH_STAR_MAP::iterator it = publish_star_map_.begin();
    for (; it != publish_star_map_.end(); ++it) {
        flash_logic::PulishStar& starpub = it->second;
        flash_logic::TimeTask time_task;
        time_task.set_symbol(starpub.symbol());
        time_task.set_earliest_time(starpub.publish_begin_time());
        time_task.set_latest_time(starpub.publish_end_time());
        //time_task.set_task_time(starpub.flash_time());
        //time_task.set_interval_time(starpub.interval_time());
        //time_task.set_task_type(starpub.publish_type());//0-预售 1-发售 2-流通
        ProcessTimeTask(time(NULL), time_task);
        flash_task_list_.push_back(time_task);
        flash_task_map_[starpub.symbol()] = time_task;
        
    }
}

//设置开始时间
void FlashManager::ProcessTimeTask(const time_t current_time, flash_logic::TimeTask& task) {

  if (current_time < task.earliest_time()) { //开始时间还未到
    task.set_task_start_time(task.earliest_time());
    task.set_task_type(TASK_PRE_SELL_TYPE);
  } else if (current_time > task.earliest_time() and current_time < task.latest_time()) {
    task.set_task_start_time(task.latest_time());
    task.set_task_type(TASK_SELLING_TYPE);
  } else if (current_time > task.latest_time()) {
    task.set_task_start_time(2145888000);
    task.set_task_type(TASK_SELL_TYPE);
  }else{
    task.set_task_start_time(2145888000);
    task.set_task_type(TASK_SELL_TYPE);
  }

}

void FlashManager::TimeStarEvent() {
  base_logic::RLockGd lk(lock_);
  time_t current_time = time(NULL);
  LOG_MSG2("flash_task_list_ size %d", flash_task_list_.size());
  if (flash_task_list_.size() <= 0)
    return;

  flash_task_list_.sort(flash_logic::TimeTask::cmp);
  while (flash_task_list_.size() > 0) {
    //达到任务时间，则执行任务
    flash_logic::TimeTask time_task = flash_task_list_.front();
    LOG_MSG2("current_time %d, time_task %d", current_time, time_task.task_start_time());
    if (current_time < time_task.task_start_time() || time_task.task_start_time() < 1262275200)
        break;
    flash_task_list_.pop_front();
    //修改状态
    LOG_MSG("NEXT STAT");
    ProcessTimeTask(current_time, time_task);
    flash_task_list_.push_back(time_task);

    flash_logic::PulishStar pubstar;
    bool r = share_memory_->GetStarfromShareMemory(time_task.symbol(), pubstar);
    if (!r) {
      continue;
    }
    flash_db_->OnUpdatePublishStarInfo(time_task.symbol(),time_task.task_type(),
                          pubstar.publish_last_time(),pubstar.publish_begin_time());
  }

}

//发送明星的交易状态
void FlashManager::FlashSymbolStatus(const int socket, const int64 session, const int32 reserved,
    const std::string& symbol) {
  bool r = false;
  flash_logic::TimeTask time_task;
  {
      base_logic::RLockGd lk(lock_);
      r = base::MapGet<TIME_TASK_MAP,TIME_TASK_MAP::iterator, std::string, flash_logic::TimeTask>
         (flash_task_map_,symbol, time_task);
  }
  if(!r) {
      send_error(socket, ERROR_TYPE, NO_HAVE_FLASH_STAR, session);
      return;
  }

  base_logic::DictionaryValue* dic = new base_logic::DictionaryValue();
  base_logic::StringValue* t_symbol = new base_logic::StringValue(symbol);
  base_logic::FundamentalValue* t_remainingTime = \
          new base_logic::FundamentalValue(time_task.task_start_time() - time(NULL));
  dic->Set(L"symbol", t_symbol);
  dic->Set(L"remainingTime", t_remainingTime);

  struct PacketControl packet_control;
  MAKE_HEAD(packet_control, S_GET_PUB_STAR_STATUS, 1, 0, session, 0);
  packet_control.body_ = dic;
  send_message(socket, &packet_control);
}

//发行明星信息
void FlashManager::SymbolInfo(const int socket, const int64 session, const int32 reserved,
    const std::string& symbol){
  flash_logic::PulishStar pubstar;
  bool r = false;
  r = share_memory_->GetStarfromShareMemory(symbol, pubstar);
  if (!r) {
    send_error(socket, ERROR_TYPE, NO_HAVE_FLASH_STAR, session);
    return ;
  }

  base_logic::DictionaryValue* dic = new base_logic::DictionaryValue();
  base_logic::StringValue* t_symbol = new base_logic::StringValue(symbol);
  base_logic::StringValue* t_starname = new base_logic::StringValue(pubstar.name());
  base_logic::StringValue* t_pic = new base_logic::StringValue(pubstar.pic());
  base_logic::StringValue* t_backpic = new base_logic::StringValue(pubstar.back_pic());
#if defined(STAR_CLOUD)
  base_logic::StringValue* t_pictail = new base_logic::StringValue(logic::SomeUtils::FindPicTail(pubstar.pic()));
  base_logic::StringValue* t_backpictail = new base_logic::StringValue(logic::SomeUtils::FindPicTail(pubstar.back_pic()));
#endif
  base_logic::FundamentalValue* t_startype = new base_logic::FundamentalValue(pubstar.star_type());
  base_logic::FundamentalValue* t_publishtype = new base_logic::FundamentalValue(pubstar.publish_type());
  base_logic::FundamentalValue* t_publishtime = new base_logic::FundamentalValue(pubstar.publish_time());
  base_logic::FundamentalValue* t_publishlasttime = new base_logic::FundamentalValue(pubstar.publish_last_time());
  base_logic::FundamentalValue* t_publishbegintime = new base_logic::FundamentalValue(pubstar.publish_begin_time());
  base_logic::FundamentalValue* t_publishendtime = new base_logic::FundamentalValue(pubstar.publish_end_time());
  base_logic::FundamentalValue* t_publishprice = new base_logic::FundamentalValue(pubstar.publish_price());


  dic->Set("star_code", t_symbol);
  dic->Set("star_name", t_starname);
  dic->Set("head_url", t_pic);
  dic->Set("back_pic_url", t_backpic);
#if defined(STAR_CLOUD)
  dic->Set("head_url_tail", t_pictail);
  dic->Set("back_pic_url_tail", t_backpictail);
#endif
  dic->Set("star_type", t_startype);
  dic->Set("publish_type", t_publishtype);
  dic->Set("publish_time", t_publishtime);
  dic->Set("publish_last_time", t_publishlasttime);
  dic->Set("publish_begin_time", t_publishbegintime);
  dic->Set("publish_end_time", t_publishendtime);
  dic->Set("publish_price", t_publishprice);

  struct PacketControl packet_control;
  MAKE_HEAD(packet_control, S_GET_PUB_STAR_INFO, 1, 0, session, 0);
  packet_control.body_ = dic;
  send_message(socket, &packet_control);
}

void FlashManager::ConfirmOrder(const int socket, const int64 session, const int32 reserved,
            const int64 uid,const std::string& symbol,const int64 amount, const double price) {
  //检查明星剩余时间
  bool r = false;
  if(!share_memory_->StarLastTimeIsEnough(symbol, amount)){
    share_memory_->JudgeStarLastTimeFinish(symbol);
    send_error(socket, ERROR_TYPE, NO_PUB_STAR_NO_TIME_ERR, session);
    return ;
  }

  star_logic::TradesOrder  flash_trades_order;
  SetFlashOrder(uid,symbol,amount,price,flash_trades_order);
  int64 result = 0;
  r = flash_db_->OnCreateFlashOrder(flash_trades_order, result);
  if(!r){
    share_memory_->JudgeStarLastTimeFinish(symbol);
    send_error(socket, ERROR_TYPE, NO_DATABASE_ERR, session);
    return ;
  }
  if(result == -1){
    share_memory_->JudgeStarLastTimeFinish(symbol);
    flash_db_->OnUpdateFlashsaleResult(flash_trades_order.order_id(), MONEY_LESS_THAN);
    //用户余额不足
    send_error(socket, ERROR_TYPE, NO_USER_BALANCE_ERR, session);
    return ;
  }
  
  //更新明星剩余时间
  int64 t_last_time = share_memory_->DeductStarLastTime(symbol, amount);
  if(-1 == t_last_time){
    //找不到明星，应该不会出现
    send_error(socket, ERROR_TYPE, NO_HAVE_FLASH_STAR, session);
    return ;
  }
  LOG_MSG2("star publish_last_time[%d]", t_last_time);
  

  flash_trades_order.set_handle_type(COMPLETE_ORDER);
  flash_trades_order.set_buy_handle_type(COMPLETE_ORDER);
  flash_trades_order.set_sell_handle_type(COMPLETE_ORDER);
  //通知确认
  SendNoiceMessage(uid, flash_trades_order.order_id(), flash_trades_order.handle_type(), session);
  //发送kafka
  flash_kafka_->SetFlashOrder(flash_trades_order);
  //更新用户余额，更新明星时间
  //double totlePrice = amount * price;
  flash_db_->OnUpdateFlashsaleResult(flash_trades_order.order_id(),COMPLETE_ORDER);

  base_logic::DictionaryValue* dic = new base_logic::DictionaryValue();
  base_logic::FundamentalValue* ret = new base_logic::FundamentalValue(1);
  dic->Set("result", ret);
  struct PacketControl packet_control;
  MAKE_HEAD(packet_control, S_CONFIRM_FLASH_ORDER, 1, 0, session, 0);
  packet_control.body_ = dic;
  send_message(socket, &packet_control);

}

void FlashManager::SetFlashOrder(const int64 uid,const std::string& symbol,const int64 amount,const double price,
                              star_logic::TradesOrder& flash_trades_order) {
    
  flash_trades_order.create_order_id();
  flash_trades_order.set_buy_position_id(-1);
  flash_trades_order.set_sell_position_id(-1);
  flash_trades_order.set_buy_uid(uid);
  flash_trades_order.set_sell_uid(-1);
  flash_trades_order.set_amount(amount);
  flash_trades_order.set_symbol(symbol);
  flash_trades_order.set_open_position_time(time(NULL));
  flash_trades_order.set_close_position_time(0);
  flash_trades_order.set_open_price(price);
  flash_trades_order.set_match_type();
  //flash_trades_order.set_handle_type(COMPLETE_ORDER);

  user_order_map_[uid] = flash_trades_order;
}

void FlashManager::SendNoiceMessage(const int64 uid, const int64 order_id,const int32 result, const int64 session) {
  star_logic::UserInfo user;
  manager_schduler_engine_->GetUserInfoSchduler(uid, &user);
  base_logic::DictionaryValue* dic = new base_logic::DictionaryValue();
  base_logic::FundamentalValue* t_orderid = new base_logic::FundamentalValue(order_id);
  base_logic::FundamentalValue* t_result = new base_logic::FundamentalValue(result);
  
  dic->Set("orderId", t_orderid);
  dic->Set("result", t_result);

  struct PacketControl packet_control;
  MAKE_HEAD(packet_control, S_ORDER_RESULT, 1, 0, session, 0);
  packet_control.body_ = dic;
  send_message(user.socket_fd(), &packet_control);
}

}


