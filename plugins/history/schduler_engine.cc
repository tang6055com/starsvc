//  Copyright (c) 2016-2017 The SWP Authors. All rights reserved.
//  Created on: 2017年1月7日 Author: kerry

#include "history/schduler_engine.h"
#include "history/history_proto_buf.h"
#include "history/operator_code.h"
#include "history/errno.h"
#include "net/comm_head.h"
#include "net/packet_processing.h"
#include "logic/logic_unit.h"
#include "basic/template.h"
#include <string>
#include <stdlib.h>

namespace history_logic {

HistoryManager* HistoryEngine::schduler_mgr_ = NULL;
HistoryEngine* HistoryEngine::schduler_engine_ = NULL;

HistoryManager::HistoryManager() {
  history_cache_ = new HistoryCache();
  Init();
}

HistoryManager::~HistoryManager() {
  DeinitThreadrw(lock_);

}

void HistoryManager::Init() {
  InitThreadrw(&lock_);
}

void HistoryManager::InitDB(history_logic::HistoryDB* history_db) {
  history_db_ = history_db;
}

void HistoryManager::InitSchdulerEngine(
    manager_schduler::SchdulerEngine* schduler_engine) {
  schduler_engine_ = schduler_engine;
}

void HistoryManager::InitHistoryWithDrawals() {
  base_logic::WLockGd lk(lock_);
  std::list<star_logic::Withdrawals> list;
  history_db_->OnHistroyWithDraw(&list);
  while (list.size() > 0) {
    star_logic::Withdrawals withdrawals = list.front();
    list.pop_front();
    SetHistoryWithDrawlsNoLock(withdrawals);
  }
}

void HistoryManager::InitHistoryTradesData() {
  base_logic::WLockGd lk(lock_);
  std::list<star_logic::TradesPosition> list;
  history_db_->OnHistroyTradesRecord(&list);
  while (list.size() > 0) {
    star_logic::TradesPosition trades = list.front();
    list.pop_front();
    SetHistoryTradesNoLock(trades);
  }
}

void HistoryManager::InitHistoryRechargeData() {
  base_logic::WLockGd lk(lock_);
  std::list<star_logic::Recharge> list;
  history_db_->OnHistroyRechargeRecord(&list);
  while (list.size() > 0) {
    star_logic::Recharge recharge = list.front();
    list.pop_front();
    SetHistoryRechargeNoLock(recharge);

  }
}

void HistoryManager::InitOwnStarData() {
  base_logic::WLockGd lk(lock_);
  std::list<star_logic::TOwnStar> list;
  history_db_->OnOwnStarRecord(&list);
  while (list.size() > 0) {
  //LOG_DEBUG2("list.size[%d]____________________________________________",list.size() );
    star_logic::TOwnStar star = list.front();
    list.pop_front();
    SetOwnStarNoLock(star);

  }
}

void HistoryManager::HandleHistoryTrade(const int socket, const int64 session,
                                        const int32 revered, const int64 uid,
                                        const int64 tid, const int32 handle) {
  //获取用户身份
  bool r = false;
  star_logic::UserInfo userinfo;
  r = schduler_engine_->GetUserInfoSchduler(uid, &userinfo);
  if (!r) {
    return;
  }

  int32 r_handle = 0;
  //操作数据库
  r = history_db_->OnHandleHistroyTrades(tid, uid, userinfo.type(), handle,
                                         r_handle);

  //操作缓存
  {
    base_logic::WLockGd lk(lock_);
    ModifyHistoryTradesNoLock(uid, tid, r_handle);
  }

  //通知客户端
  history_logic::net_reply::HistoryHandle net_history_handle;
  net_history_handle.set_id(uid);
  net_history_handle.set_handle(r_handle);
  struct PacketControl packet_control;
  MAKE_HEAD(packet_control, R_HISTORY_TRADES_HANDLE, 1, 0, session, 0);
  packet_control.body_ = net_history_handle.get();
  send_message(socket, &packet_control);
}

void HistoryManager::SendHistoryWithDrawls(const int socket,
                                           const int64 session,
                                           const int32 revered, const int64 uid,
                                           const int32 status, const int64 pos,
                                           const int64 count) {
  std::list<star_logic::Withdrawals> withdrawals_list;
  {
    base_logic::RLockGd lk(lock_);  //1:处理中,2:成功,3:失败
    GetHistoryDrawlNoLock(uid, status, withdrawals_list, 0, 0);
  }

  //没有对应的历史记录
  if (withdrawals_list.size() <= 0) {
    send_error(socket, ERROR_TYPE, NO_HAVE_HISTROY_DATA_DRAWLS, session);
    return;
  }
  int32 base_num = 10;
  if (revered / 1000 == HTTP)
    base_num = count;
  else
    base_num = base_num < count ? base_num : count;

  int32 t_start = 0;
  int32 t_count = 0;

  history_logic::net_reply::AllWithDraw net_allwithdraw;
  withdrawals_list.sort(star_logic::Withdrawals::close_after);
  while (withdrawals_list.size() > 0 && t_count < count) {
    star_logic::Withdrawals withdrawals = withdrawals_list.front();
    withdrawals_list.pop_front();
    t_start++;
    if (t_start < pos)
      continue;
    net_reply::WithDrawals* net_withdrawals = new net_reply::WithDrawals;
    net_withdrawals->set_amount(withdrawals.amount());
    net_withdrawals->set_bank(withdrawals.bank());
    net_withdrawals->set_branch_bank(withdrawals.branch_bank());
    net_withdrawals->set_card_no(withdrawals.card_no());
    net_withdrawals->set_charge(withdrawals.charge());
    net_withdrawals->set_commet(withdrawals.comment());
    net_withdrawals->set_handle_time(withdrawals.handle_time());
    net_withdrawals->set_id(withdrawals.uid());
    net_withdrawals->set_name(withdrawals.name());
    net_withdrawals->set_status(withdrawals.status());
    net_withdrawals->set_wid(withdrawals.wid());
    net_withdrawals->set_withdraw_time(withdrawals.withdraw_time());
    net_allwithdraw.set_unit(net_withdrawals->get());
    t_count++;
    if (net_allwithdraw.Size() % base_num == 0 && net_allwithdraw.Size() != 0) {
      struct PacketControl packet_control;
      MAKE_HEAD(packet_control, S_HISTORY_RECHARGE, 1, 0, session, 0);
      packet_control.body_ = net_allwithdraw.get();
      send_message(socket, &packet_control);
      net_allwithdraw.Reset();
    }
  }

  if (net_allwithdraw.Size() > 0) {
    struct PacketControl packet_control;
    MAKE_HEAD(packet_control, S_HISTORY_RECHARGE, 1, 0, session, 0);
    packet_control.body_ = net_allwithdraw.get();
    send_message(socket, &packet_control);
  }
}

void HistoryManager::SendHistoryRecharge(const int socket, const int64 session,
                                         const int32 revered, const int64 uid,
                                         const int32 status, const int64 pos,
                                         const std::string queryTime, const int64 count) {
  std::list<star_logic::Recharge> recharge_list;
  {
    base_logic::RLockGd lk(lock_);  //1:处理中,2:成功,3:失败
    GetHistoryRechargeNoLock(uid, status, queryTime, recharge_list, 0, 0);
  }

  //没有对应的历史记录
  if (recharge_list.size() <= 0) {
    send_error(socket, ERROR_TYPE, NO_HAVE_HISTROY_DATA_RECHARGE, session);
    return;
  }
  int32 base_num = 10;
  if (revered / 1000 == HTTP)
    base_num = count;
  else
    base_num = base_num < count ? base_num : count;

  int32 t_start = 0;
  int32 t_count = 0;

  history_logic::net_reply::AllRecharge all_net_rechagre;
  recharge_list.sort(star_logic::Recharge::close_after);
  while (recharge_list.size() > 0 && t_count < count) {
    star_logic::Recharge recharge = recharge_list.front();
    recharge_list.pop_front();
    t_start++;
    if (t_start < pos)
      continue;
    net_reply::Recharge* net_recharge = new net_reply::Recharge;
    net_recharge->set_rid(recharge.rid());
    net_recharge->set_amount(recharge.amount());
    net_recharge->set_deposit_name(recharge.deposit_name());
    net_recharge->set_deposit_time(recharge.deposit_time());
    net_recharge->set_deposit_type(recharge.deposit_type());
    net_recharge->set_status(recharge.status());
    net_recharge->set_recharge_type(recharge.recharge_type());
    all_net_rechagre.set_unit(net_recharge->get());
    t_count++;
    if (all_net_rechagre.Size() % base_num == 0
        && all_net_rechagre.Size() != 0) {
      struct PacketControl packet_control;
      MAKE_HEAD(packet_control, S_HISTORY_RECHARGE, 1, 0, session, 0);
      packet_control.body_ = all_net_rechagre.get();
      send_message(socket, &packet_control);
      all_net_rechagre.Reset();
    }
  }

  if (all_net_rechagre.Size() > 0) {
    struct PacketControl packet_control;
    MAKE_HEAD(packet_control, S_HISTORY_RECHARGE, 1, 0, session, 0);
    packet_control.body_ = all_net_rechagre.get();
    send_message(socket, &packet_control);
  }
}

void HistoryManager::SendHistoryOwnStar(const int socket, const int64 session,
                                         const int32 revered, const int64 uid,
                                         const int32 status, const int64 pos,
                                         const int64 count) {
  std::list<star_logic::TOwnStar> ownstar_list;
  //LOG_DEBUG2("packet_length %d____________________________________________",ownstar_list.size() );
  {
    base_logic::RLockGd lk(lock_);  //
    GetHistoryOwnStarNoLock(uid, status, ownstar_list, 0, 0);
  }

  //LOG_DEBUG2("packet_length %d____________________________________________",ownstar_list.size() );
  //没有对应的历史记录
  if (ownstar_list.size() <= 0) {
    send_error(socket, ERROR_TYPE, NO_HAVE_HISTROY_DATA_OWNSTAR, session);
    return;
  }
/*
    LOG_DEBUG2("packet_length %d____________________________________________",ownstar_list.size() );
    LOG_DEBUG2("packet_length %d____________________________________________",ownstar_list.size() );
    LOG_DEBUG2("packet_length %d____________________________________________",ownstar_list.size() );
    LOG_DEBUG2("packet_length %d____________________________________________",ownstar_list.size() );
*/
  int32 base_num = 10;
  if (revered / 1000 == HTTP)
    base_num = count;
  else
    base_num = base_num < count ? base_num : count;

  int32 t_start = 0;
  int32 t_count = 0;

  history_logic::net_reply::AllOwnStar all_net_ownstar;
  //ownstar_list.sort(star_logic::Recharge::close_after);
  while (ownstar_list.size() > 0 && t_count < count) {
    star_logic::TOwnStar ownstar = ownstar_list.front();
    ownstar_list.pop_front();
    t_start++;
    if (t_start < pos)
      continue;

    net_reply::OwnStar* net_ownstar = new net_reply::OwnStar;

    net_ownstar->set_uid(ownstar.uid());
    net_ownstar->set_ownseconds(ownstar.ownseconds());
    net_ownstar->set_appoint(ownstar.appoint());
    net_ownstar->set_starcode(ownstar.starcode());
    net_ownstar->set_starname(ownstar.starname());
    net_ownstar->set_faccid(ownstar.faccid());
    net_ownstar->set_headurl(ownstar.headurl());

    all_net_ownstar.set_unit(net_ownstar->get());
    t_count++;
    if (all_net_ownstar.Size() % base_num == 0
        && all_net_ownstar.Size() != 0) {
      struct PacketControl packet_control;
      MAKE_HEAD(packet_control, S_HISTORY_RECHARGE, 1, 0, session, 0);
      packet_control.body_ = all_net_ownstar.get();
      send_message(socket, &packet_control);
      all_net_ownstar.Reset();
    }
  }

  if (all_net_ownstar.Size() > 0) {
    struct PacketControl packet_control;
    MAKE_HEAD(packet_control, S_HISTORY_OWNSTAR, 1, 0, session, 0);
    packet_control.body_ = all_net_ownstar.get();
    send_message(socket, &packet_control);
  }
}

void HistoryManager::SendHandlePosition(const int socket, const int64 session,
                                        const int32 reversed, const int64 uid,
                                        const int32 htype, const int64 pos,
                                        const int64 count) {
  std::list<star_logic::TradesPosition> trades_list;
  {
    base_logic::RLockGd lk(lock_);
    GetHistoryTradesNoLock(uid, trades_list, 0, 0);
  }

  //没有对应的历史记录
  if (trades_list.size() <= 0) {
    send_error(socket, ERROR_TYPE, NO_HAVE_HADNLE_DATA, session);
    return;
  }
  int32 base_num = 10;
  if (reversed / 1000 == HTTP)
    base_num = count;
  else
    base_num = base_num < count ? base_num : count;

  int32 t_start = 0;
  int32 t_count = 0;

  history_logic::net_reply::AllTradesPosition net_trades_positions;
  trades_list.sort(star_logic::TradesPosition::close_after);
  while (trades_list.size() > 0 && t_count < count) {
    star_logic::TradesPosition trades_position = trades_list.front();
    trades_list.pop_front();
    t_start++;
    //trades_position.symbol() != symbol && symbol != "all"

    //if ((trades_position.handle() != htype && htype != 10) || trades_position.handle()== NO_HANDLE)
    //continue;

    if (trades_position.handle() == NO_HANDLE)
      continue;

    if (htype != 10)
      if (trades_position.handle() != htype)
        continue;

    if (t_start < pos)
      continue;

    net_reply::TradesPosition* net_trades_position =
        new net_reply::TradesPosition;
    net_trades_position->set_amount(trades_position.amount());
    net_trades_position->set_buy_sell(trades_position.buy_sell());
    net_trades_position->set_close_price(trades_position.close_price());
    net_trades_position->set_close_time(trades_position.close_position_time());
    net_trades_position->set_close_type(trades_position.close_type());
    net_trades_position->set_gross_profit(trades_position.gross_profit());
    net_trades_position->set_code_id(trades_position.code_id());
    net_trades_position->set_deferred(trades_position.deferred());
    net_trades_position->set_gross_profit(trades_position.gross_profit());
    net_trades_position->set_id(trades_position.uid());
    net_trades_position->set_close_type(trades_position.close_type());
    net_trades_position->set_interval(
        trades_position.close_position_time()
            - trades_position.open_position_time());
    net_trades_position->set_result(trades_position.result());
    net_trades_position->set_is_deferred(trades_position.is_deferred());
    net_trades_position->set_limit(trades_position.limit());
    net_trades_position->set_name(trades_position.name());
    net_trades_position->set_open_charge(trades_position.open_charge());
    net_trades_position->set_open_cost(trades_position.open_cost());
    net_trades_position->set_open_price(trades_position.open_price());
    net_trades_position->set_position_id(trades_position.position_id());
    net_trades_position->set_position_time(
        trades_position.open_position_time());
    net_trades_position->set_stop(trades_position.stop());
    net_trades_position->set_symbol(trades_position.symbol());
    net_trades_position->set_handle(trades_position.handle());
    net_trades_positions.set_unit(net_trades_position->get());
    t_count++;
    if (net_trades_positions.Size() % base_num == 0
        && net_trades_positions.Size() != 0) {
      struct PacketControl packet_control;
      MAKE_HEAD(packet_control, S_HISTORY_TRADES, HISTORY_TYPE, 0, session, 0);
      packet_control.body_ = net_trades_positions.get();
      send_message(socket, &packet_control);
      net_trades_positions.Reset();
    }
  }

  if (net_trades_positions.Size() > 0) {
    struct PacketControl packet_control;
    MAKE_HEAD(packet_control, S_HISTORY_TRADES, HISTORY_TYPE, 0, session, 0);
    packet_control.body_ = net_trades_positions.get();
    send_message(socket, &packet_control);
  }
}

void HistoryManager::GetHistoryDrawlNoLock(
    const int64 uid, const int32 status,
    std::list<star_logic::Withdrawals>& list, const int64 pos,
    const int64 count) {
  WITHDRAWALS_MAP withdrawals_map;
  base::MapGet<ALLWITHDRAWALS_MAP, ALLWITHDRAWALS_MAP::iterator, int64,
      WITHDRAWALS_MAP>(history_cache_->all_withdrawals_map_, uid,
                       withdrawals_map);

  for (WITHDRAWALS_MAP::iterator it = withdrawals_map.begin();
      it != withdrawals_map.end(); it++) {
    star_logic::Withdrawals withdrawals = it->second;
    list.push_back(withdrawals);
  }
}

void HistoryManager::GetHistoryTradesNoLock(
    const int64 uid, std::list<star_logic::TradesPosition>& list,
    const int64 pos, const int64 count) {
  TRADES_MAP trades_map;
  base::MapGet<ALL_TRADES_MAP, ALL_TRADES_MAP::iterator, int64, TRADES_MAP>(
      history_cache_->all_trades_map_, uid, trades_map);

  for (TRADES_MAP::iterator it = trades_map.begin(); it != trades_map.end();
      it++) {
    star_logic::TradesPosition trades = it->second;
    list.push_back(trades);
  }
}

void HistoryManager::GetHistoryRechargeNoLock(
    const int64 uid, const int32 status,
    const std::string queryTime, std::list<star_logic::Recharge>& list,
    const int64 pos, const int64 count) {
  RECHARGE_MAP recharge_map;
  base::MapGet<ALL_RECHAGE_MAP, ALL_RECHAGE_MAP::iterator, int64, RECHARGE_MAP>(
      history_cache_->all_rechage_map_, uid, recharge_map);

  time_t tt = time(NULL);
  struct tm t;
  localtime_r(&tt, &t);
  std::string tmpQt;//YYYY-MM
  char cComMon[8];
  if(queryTime != ""){
  	if(queryTime.length()<2)tmpQt="0"+queryTime;//01-12
  	sprintf(cComMon,"%d-%s",(1900+t.tm_year),tmpQt.c_str());
    tmpQt = cComMon;
  }
  
  LOG_DEBUG2("GetHistoryRechargeNoLock querytime %s",tmpQt.c_str());
  for (RECHARGE_MAP::iterator it = recharge_map.begin();
      it != recharge_map.end(); it++) {
    star_logic::Recharge recharge = it->second;
    std::string depositMon = recharge.deposit_time().substr(0,7); //YYYY-MM-DD HH:mm:ss
    if(queryTime == ""){
      list.push_back(recharge);
    }else if(depositMon == tmpQt){
      list.push_back(recharge);
    }
  }
}

void HistoryManager::GetHistoryOwnStarNoLock(
    const int64 uid, const int32 status, std::list<star_logic::TOwnStar>& list,
    const int64 pos, const int64 count) {
  //LOG_DEBUG2("GetHistoryOwnStarNoLock________________ [%d] _ [%d]",uid,  history_cache_->all_ownstar_map_.size() );
  OWNSTAR_MAP ownstar_map;
  base::MapGet<ALL_OWNSTAR_MAP, ALL_OWNSTAR_MAP::iterator, int64, OWNSTAR_MAP>(
      history_cache_->all_ownstar_map_, uid, ownstar_map);

  for (OWNSTAR_MAP::iterator it = ownstar_map.begin();
      it != ownstar_map.end(); it++) {
    star_logic::TOwnStar ownstar = it->second;
    list.push_back(ownstar);
  }
}

void HistoryManager::SetHistoryTradesNoLock(star_logic::TradesPosition& trades) {
  TRADES_MAP trades_map;
  base::MapGet<ALL_TRADES_MAP, ALL_TRADES_MAP::iterator, int64, TRADES_MAP>(
      history_cache_->all_trades_map_, trades.uid(), trades_map);
  trades_map[trades.position_id()] = trades;
  history_cache_->all_trades_map_[trades.uid()] = trades_map;
}

void HistoryManager::SetHistoryRechargeNoLock(star_logic::Recharge& recharge) {
  RECHARGE_MAP recharge_map;
  base::MapGet<ALL_RECHAGE_MAP, ALL_RECHAGE_MAP::iterator, int64, RECHARGE_MAP>(
      history_cache_->all_rechage_map_, recharge.uid(), recharge_map);
  recharge_map[recharge.rid()] = recharge;
  history_cache_->all_rechage_map_[recharge.uid()] = recharge_map;
}

void HistoryManager::SetOwnStarNoLock(star_logic::TOwnStar& ownstar) {

  //LOG_DEBUG("test ___ownstar.size[%]____________________________________________" );
  OWNSTAR_MAP ownstar_map;

  //LOG_DEBUG("test222 ___ownstar.size[%]____________________________________________" );
  base::MapGet<ALL_OWNSTAR_MAP, ALL_OWNSTAR_MAP::iterator, int64, OWNSTAR_MAP>(
      history_cache_->all_ownstar_map_, ownstar.uid(), ownstar_map);

  //LOG_DEBUG("test333 ___ownstar.size[%]____________________________________________" );
  //history_cache_->all_ownstar_map_.find(ownstar.uid());
  ownstar_map[atoll(ownstar.starcode().c_str())] = ownstar;
  //LOG_DEBUG2("ownstar.size[%d]__________ starcode[%s] __________________________________",ownstar_map.size() , ownstar.starcode().c_str());
  history_cache_->all_ownstar_map_[ownstar.uid()] = ownstar_map;
  //LOG_DEBUG2("allownstar.size[%d]ownstar.uid[%d]____________________________________________",history_cache_->all_ownstar_map_.size(), ownstar.uid() );


}

void HistoryManager::SetHistoryWithDrawlsNoLock(
    star_logic::Withdrawals& withdrawls) {
  WITHDRAWALS_MAP withdrawals_map;
  base::MapGet<ALLWITHDRAWALS_MAP, ALLWITHDRAWALS_MAP::iterator, int64,
      WITHDRAWALS_MAP>(history_cache_->all_withdrawals_map_, withdrawls.uid(),
                       withdrawals_map);
  withdrawals_map[withdrawls.wid()] = withdrawls;
  history_cache_->all_withdrawals_map_[withdrawls.uid()] = withdrawals_map;
}

void HistoryManager::ModifyHistoryTradesNoLock(const int64 uid, const int64 tid,
                                               const int32 handle) {
  TRADES_MAP trades_map;
  star_logic::TradesPosition trades_position;
  bool r = base::MapGet<ALL_TRADES_MAP, ALL_TRADES_MAP::iterator, int64,
      TRADES_MAP>(history_cache_->all_trades_map_, uid, trades_map);
  if (!r)
    return;

  r = base::MapGet<TRADES_MAP, TRADES_MAP::iterator, int64,
      star_logic::TradesPosition>(trades_map, tid, trades_position);
  trades_position.set_handle(handle);

}

}
