/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#include "os.h"
#include "taosdef.h"
#include "tsched.h"
#include "tbalance.h"
#include "tgrant.h"
#include "tglobal.h"
#include "trpc.h"
#include "mnode.h"
#include "dnode.h"
#include "mnodeDef.h"
#include "mnodeInt.h"
#include "mnodeAcct.h"
#include "mnodeDnode.h"
#include "mnodeMnode.h"
#include "mnodeDb.h"
#include "mnodeSdb.h"
#include "mnodeVgroup.h"
#include "mnodeUser.h"
#include "mnodeTable.h"
#include "mnodeShow.h"

static int32_t (*tsMnodeProcessWriteMsgFp[TSDB_MSG_TYPE_MAX])(SMnodeMsg *);

void mnodeAddWriteMsgHandle(uint8_t msgType, int32_t (*fp)(SMnodeMsg *mnodeMsg)) {
  tsMnodeProcessWriteMsgFp[msgType] = fp;
}

int32_t mnodeProcessWrite(SMnodeMsg *pMsg) {
  if (pMsg->rpcMsg.pCont == NULL) {
    mError("app:%p:%p, msg:%s content is null", pMsg->rpcMsg.ahandle, pMsg, taosMsg[pMsg->rpcMsg.msgType]);
    return TSDB_CODE_MND_INVALID_MSG_LEN;
  }

  if (!sdbIsMaster()) {
    SMnodeRsp *rpcRsp = &pMsg->rpcRsp;
    SRpcIpSet *ipSet = rpcMallocCont(sizeof(SRpcIpSet));
    mnodeGetMnodeIpSetForShell(ipSet);
    rpcRsp->rsp = ipSet;
    rpcRsp->len = sizeof(SRpcIpSet);

    mDebug("app:%p:%p, msg:%s will be redireced inUse:%d", pMsg->rpcMsg.ahandle, pMsg, taosMsg[pMsg->rpcMsg.msgType],
           ipSet->inUse);
    for (int32_t i = 0; i < ipSet->numOfIps; ++i) {
      mDebug("app:%p:%p, mnode index:%d ip:%s:%d", pMsg->rpcMsg.ahandle, pMsg, i, ipSet->fqdn[i],
             htons(ipSet->port[i]));
    }

    return TSDB_CODE_RPC_REDIRECT;
  }

  if (tsMnodeProcessWriteMsgFp[pMsg->rpcMsg.msgType] == NULL) {
    mError("app:%p:%p, msg:%s not processed", pMsg->rpcMsg.ahandle, pMsg, taosMsg[pMsg->rpcMsg.msgType]);
    return TSDB_CODE_MND_MSG_NOT_PROCESSED;
  }

  int32_t code = mnodeInitMsg(pMsg);
  if (code != TSDB_CODE_SUCCESS) {
    mError("app:%p:%p, msg:%s not processed, reason:%s", pMsg->rpcMsg.ahandle, pMsg, taosMsg[pMsg->rpcMsg.msgType],
           tstrerror(code));
    return code;
  }

  if (!pMsg->pUser->writeAuth) {
    mError("app:%p:%p, msg:%s not processed, no write auth", pMsg->rpcMsg.ahandle, pMsg,
           taosMsg[pMsg->rpcMsg.msgType]);
    return TSDB_CODE_MND_NO_RIGHTS;
  }

  return (*tsMnodeProcessWriteMsgFp[pMsg->rpcMsg.msgType])(pMsg);
}
