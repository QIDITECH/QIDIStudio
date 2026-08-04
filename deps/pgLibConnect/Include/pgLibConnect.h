/****************************************************************
  copyright   : Copyright (C) 2013, chenbichao,
              : All rights reserved.
              : www.pptun.com, www.peergine.com
              : 
  filename    : pgLibConnect.h
  discription : 
  modify      : create, chenbichao, 2013/11/10
              :
			  : modify, chenbichao, 2013/12/18
			  : 1. pgEvent()函数的uTimeout参数，改为传0时不等待，立即返回。
			  :
			  : modify, chenbichao, 2013/12/30
			  : 1. pgEvent()接口函数增加上报上线和下线两个事件
			  :
			  : modify, chenbichao, 2014/03/17
			  : 1. PG_INIT_CFG_S结构增加uTryP2PTimeout属性，用来指定P2P穿透的超时时间。
			  :
			  : modify, chenbichao, 2014/03/26
			  : 1. 增加PG_EVENT_OFFLINE事件，用来指定连接时对端不在线。
			  :
			  : modify, chenbichao, 2014/04/01
			  : 1. 增加PG_EVENT_INFO事件，指示本端或对端的NAT类型、地址信息监测状态。
			  : 2. 增加“PG_CNNT_E”枚举，表示连接的类型（NAT类型）
			  : 3. 结构“PG_INFO_S”的uIsForward属性改为uCnntType，表示连接类型。
			  :
			  : modify, chenbichao, 2014/04/02
			  : 1. 增加pgLevel()接口函数，用来开启和关闭不同级别日志输出。
			  :
			  : modify, chenbichao, 2014/05/22
			  : 1. 增加PG_CNNT_IPV4_PeerFwd连接类型，借助P2P节点来转发流量。
			  : 2. PG_INIT_CFG_S结构增加uAllowForward属性，允许其他P2P节点借助本节点转发流量。
			  :
			  : modify, chenbichao, 2014/05/30
			  : 1. PG_INIT_CFG_S结构的uAllowForward属性改为uForwardSpeed，允许设置转发的带宽限制。
			  :
			  : modify, chenbichao, 2014/10/29
			  : 1. 增加PG_EVENT_LAN_SCAN事件，表示局域网P2P节点搜索结束了。
			  : 2. 增加PG_LAN_SCAN_S数据结构，用来获取局域网P2P节点搜索的结果。
			  : 3. 增加pgLanScanStart()函数，发起局域网内的P2P节点搜索
			  : 4. 增加pgLanScanResult()函数，获取局域网内的P2P节点搜索的结果
			  :
			  : modify, chenbichao, 2015/01/18
			  : 1. PG_INIT_CFG_S结构的uSessTimeout属性的缺省值改为30。
			  :
			  : modify, chenbichao, 2015/03/30
			  : 1. 增加PG_NET_MODE_E选择网络方式枚举。
			  : 2. 增加pgServerNetMode()函数，切换连接P2P服务器的网络方式。
			  : 
			  : modify, chenbichao, 2015/04/21
			  : 1. 增加PG_INFO_S结构的szListenID属性，用来获取侦听端的ID。
			  : 2. 修改PG_LAN_SCAN_S结构的szPeerID为szListenID，避免歧义。
			  : 3. 增加pgConnected()函数，判断会话连接有没有完成。
			  : 
			  : modify, chenbichao, 2015/12/12
			  : 1. 增加PG_INIT_CFG_S结构的uForwardUse属性，可以设置是否使用第3方P2P节点进行转发。
			  : 2. 修改PG_INIT_CFG_S结构的uParam属性为void指针类型，可以兼容64bit的使用场景。
			  : 3. 增加pgPeek()函数，可查看当前需要接收的数据的长度和优先级。
			  : 
*****************************************************************/
#ifndef _PG_LIB_CONNECT_H
#define _PG_LIB_CONNECT_H


#ifdef __cplusplus
extern "C" {
#endif


/**
 *  运行模式：客户端或侦听端
 */
typedef enum tagPG_MODE_E {
	PG_MODE_CLIENT,   // 客户端
	PG_MODE_LISTEN,   // 侦听端（通常是设备端）
	PG_MODE_BUTT,
} PG_MODE_E;


/**
 *  错误码定义
 */
typedef enum tagPG_ERROR_E {
	PG_ERROR_OK = 0,             // 成功
	PG_ERROR_INIT = -1,          // 没有调用pgInitialize()或者已经调用pgCleanup()清理模块。
	PG_ERROR_CLOSE = -2,         // 会话已经关闭（会话已经不可恢复）。
	PG_ERROR_BADPARAM = -3,      // 传递的参数错误。
	PG_ERROR_NOBUF = -4,         // 会话发送缓冲区已满。
	PG_ERROR_NODATA = -5,        // 会话没有数据到达。
	PG_ERROR_NOSPACE = -6,       // 传递的接收缓冲区太小。
	PG_ERROR_TIMEOUT = -7,       // 操作超时。
	PG_ERROR_BUSY = -8,          // 系统正忙。
	PG_ERROR_NOLOGIN = -9,       // 还没有登录到P2P服务器。
	PG_ERROR_MAXSESS = -10,      // 会话数限制
	PG_ERROR_NOCONNECT = -11,    // 会话还没有连接完成
	PG_ERROR_MAXINST = -12,      // 实例数限制
	PG_ERROR_SYSTEM = -127,      // 系统错误。
} PG_ERROR_E;


/**
 *  pgEvent()函数等待的事件定义
 */
typedef enum tagPG_EVENT_E {
	PG_EVENT_NULL = 0,                   // NULL
	PG_EVENT_CONNECT = 1,                // 会话连接成功了，可以调用pgWrite()发送数据。
	PG_EVENT_CLOSE = 2,                  // 会话被对端关闭，需要调用pgClose()才能彻底释放会话资源。
	PG_EVENT_WRITE = 3,                  // 会话的底层发送缓冲区的空闲空间增加了，可以调用pgWrite()发送新数据。
	PG_EVENT_READ = 4,                   // 会话的底层接收缓冲区有数据到达，可以调用pgRead()接收新数据。
	PG_EVENT_OFFLINE = 5,                // 会话的对端不在线了，调用pgOpen()后，如果对端不在线，则上报此事件。
	PG_EVENT_INFO = 6,                   // 会话的连接方式或NAT类型检测有变化了，可以调用pgInfo()获取最新的连接信息。

	PG_EVENT_SVR_LOGIN = 16,             // 登录到P2P服务器成功（上线）
	PG_EVENT_SVR_LOGOUT = 17,            // 从P2P服务器注销或掉线（下线）
	PG_EVENT_SVR_REPLY = 18,             // P2P服务器应答事件，可以调用pgServerReply()接收应答。
	PG_EVENT_SVR_NOTIFY = 19,            // P2P服务器推送事件，可以调用pgServerNotify()接收推送。
	PG_EVENT_SVR_ERROR = 20,             // pgServerRequest返回错误，在pgEvent()的 lpuSessIDNow 参数输出错误码。
	PG_EVENT_SVR_KICK_OUT = 21,          // 被服务器踢出，因为有另外一个相同ID的节点登录了。
	PG_EVENT_SVR_LOGIN_ERROR = 22,       // 登录到P2P服务器失败，在pgEvent()的 lpuSessIDNow 参数输出错误码

	PG_EVENT_LAN_SCAN = 32,              // 扫描局域网的P2P节点返回事件。可以调用pgLanScanResult()去接收结果。
	PG_EVENT_PEER_FWD_STATUS = 33,       // 节点转发的状态信息，调用pgPeerFwdStatus()获取
	PG_EVENT_PEER_FWD_STATISTIC = 34,    // 节点转发的（贡献）统计信息，调用pgPeerFwdStatistic()获取
	PG_EVENT_PEER_FWD_STAT_USED = 35,    // 节点转发的（消费）统计信息，调用pgPeerFwdStatUsed()获取

} PG_EVENT_E;


/**
 *  数据发送/接收优先级
 */
typedef enum tagPG_PRIORITY_E {
	PG_PRIORITY_0,         // 优先级0, 最高优先级。（这个优先级上不能发送太大流量的数据，因为可能会影响P2P模块本身的握手通信。）
	PG_PRIORITY_1,         // 优先级1
	PG_PRIORITY_2,         // 优先级2
	PG_PRIORITY_3,         // 优先级3, 最低优先级
	PG_PRIORITY_BUTT,
} PG_PRIORITY_E;


/**
 *  通道的连接类型
 */
typedef enum tagPG_CNNT_E {
	PG_CNNT_Unknown = 0,            // 未知，还没有检测到连接类型

	PG_CNNT_IPV4_Pub = 4,           // 公网IPv4地址
	PG_CNNT_IPV4_NATConeFull = 5,   // 完全锥形NAT
	PG_CNNT_IPV4_NATConeHost = 6,   // 主机限制锥形NAT
	PG_CNNT_IPV4_NATConePort = 7,   // 端口限制锥形NAT
	PG_CNNT_IPV4_NATSymmet = 8,     // 对称NAT

	PG_CNNT_IPV4_Private = 12,      // 私网直连
	PG_CNNT_IPV4_NATLoop = 13,      // 私网NAT环回

	PG_CNNT_IPV4_TunnelTCP = 16,    // TCPv4转发
	PG_CNNT_IPV4_TunnelHTTP = 17,   // HTTPv4转发

	PG_CNNT_IPV4_PeerFwd = 24,      // 借助P2P节点转发

	PG_CNNT_IPV6_Pub = 32,          // 公网IPv6地址
	PG_CNNT_IPV6_Local = 36,        // 私网IPv6地址

	PG_CNNT_IPV6_TunnelTCP = 40,    // TCPv6转发
	PG_CNNT_IPV6_TunnelHTTP = 41,   // HTTPv6转发

	PG_CNNT_P2P = 128,              // P2P连接
	PG_CNNT_Local = 129,            // 私网连接
	PG_CNNT_PeerForward = 130,      // 节点转发连接
	PG_CNNT_RelayForward = 131,     // 中继转发连接

	PG_CNNT_Offline = 0xffff,       // 对端不在线

} PG_CNNT_E;


/**
 *  节点转发的状态
 */
typedef enum tagPG_PEER_FWD_STA_E {
	PG_PEER_FWD_STA_DISABLE = 0,    // 节点转发为禁用。
	PG_PEER_FWD_STA_PAUSE = 1,      // 节点转发为暂停，因为本端正在使用网络带宽。
	PG_PEER_FWD_STA_IDLE = 2,       // 节点转发为启用，且当前空闲。
	PG_PEER_FWD_STA_USED = 3,       // 节点转发为启用，且当前正在协助转发。
} PG_PEER_FWD_STA_E;


/**
 * 选择网络的方式
 */
typedef enum tagPG_NET_MODE_E {
	PG_NET_MODE_Auto,               // 自动选择网络
	PG_NET_MODE_P2P,                // 只使用P2P穿透
	PG_NET_MODE_Relay,              // 只使用Relay转发
} PG_NET_MODE_E;


/**
 *  初始化参数。
 */
typedef struct tagPG_INIT_CFG_S {

	// 4个优先级的发送缓冲区长度，单位为：K字节。
	// uBufSize[0] 为优先级0的发送缓冲区长度，传0则使用缺省值，缺省值为128(K)
	// uBufSize[1] 为优先级1的发送缓冲区长度，传0则使用缺省值，缺省值为128(K)
	// uBufSize[2] 为优先级2的发送缓冲区长度，传0则使用缺省值，缺省值为256(K)
	// uBufSize[3] 为优先级3的发送缓冲区长度，传0则使用缺省值，缺省值为256(K)
	// 提示：缓冲区的内存不是初始化时就分配好，要用的时候才分配。
	//       例如，配置了256(K)，但当前只使用了16(K)，则只分配16(K)的内存。
	//       如果网络带宽大，发送的数据不在缓冲区中滞留，则缓冲区实际使用的长度不会增长。
	// 注意：缓冲区的长度值不能超过32768。
	unsigned int uBufSize[PG_PRIORITY_BUTT];

	// 会话尝试连接的超时时间。传入0则使用缺省值，缺省值为30秒。
	// 在这段时间内，如果连接不成功，则上报PG_EVENT_CLOSE事件。
	unsigned int uSessTimeout;

	// 尝试P2P穿透的时间。这个时间到达后还没有穿透，则切换到转发通信。
	// (uTryP2PTimeout == 0)：使用缺省值，缺省值为6秒。
	// (uTryP2PTimeout > 0 && uTryP2PTimeout <= 3600)：超时值为所传的uTryP2PTimeout
	// (uTryP2PTimeout > 3600)：禁用P2P穿透，直接用转发。
	// !! [此参数已经弃用，请参考 pgSetConfig() 函数的'(P2PTryTime){}'参数]
	unsigned int uTryP2PTimeout;

	// 允许其他P2P节点借助本节点转发流量。非0：转发速率（字节/秒），0：不允许转发。
	// 建议配置: 32K (字节/秒) 以上。
	// !! [此参数已经弃用，请参考 pgSetConfig() 函数的'(ForwardSpeed){}'参数]
	unsigned int uForwardSpeed;

	// 是否借助第3方P2P节点转发流量。非0：是，0：否。
	// !! [此参数已经弃用，请参考 pgSetConfig() 函数的'(ForwardUse){}'参数]
	unsigned int uForwardUse;

	// 传递初始化参数（目前在Android系统传入Java VM的指针。）
	// 在JNI模块中实现JNI_Onload接口，获取到Java VM的指针，并在pgInitialize()传入给P2P模块。
	void* pvParam;
								
} PG_INIT_CFG_S;


/**
 *  会话信息。
 */
typedef struct tagPG_INFO_S {
	char szPeerID[128];             // 会话对端的P2P ID
	char szAddrPub[64];             // 会话对端的公网IP地址
	char szAddrPriv[64];            // 会话对端的私网IP地址
	char szListenID[128];           // 会话侦听端的ID（在客户端调用pgInfo()时有效）。
	unsigned int uCnntType;         // 会话通道的连接类型（NAT类型），见枚举“PG_CNNT_E”
} PG_INFO_S;


/**
 * 局域网扫描结果
 */
typedef struct tagPG_LAN_SCAN_S {
	char szAddr[64];                // 侦听端的局域网IP地址
	char szListenID[128];           // 侦听端的ID
} PG_LAN_SCAN_S;


/**
 *  日志输出回调函数
 *
 *  uLevel：[IN] 日志级别
 *
 *  lpszOut：[IN] 日志输出内容
 */
typedef void (*TfnLogOut)(unsigned int uLevel, const char* lpszOut);


/**
 *  描述：设置公共的配置参数
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  lpszParam [IN] 配置的参数内容。
 *      字符串方式的参数项，圆括号内为参数名，花括号内为参数值。
 *      格式示例：(Digest){0}(Debug){0}(DnsRandom){1}(DnsUseIPv4){1}(DnsUseIPv6){1}
 *      
 *      参数项：
 *          Digest: 是否启用摘要认证方式。0:禁用，1:启用，默认为0
 *          Debug: 是否开启调试信息输出。0:禁用，1:启用，默认为0
 *          DnsRandom: 是否开启随机选取DNS解析返回IP地址。0:禁用，1:启用，默认为1
 *          DnsUseIPv4: 是否使用DNS解析返回IPv4地址。0:禁用，1:启用，默认为1
 *          DnsUseIPv6: 是否使用DNS解析返回IPv6地址。0:禁用，1:启用，默认为1
 *          ... 其他参数：参考API说明文档。
 *
 *
 *  返回值：见枚举‘PG_ERROR_E’的定义
 */
int pgSetConfig(const char* lpszParam);


/**
 *  描述：P2P穿透模块初始化函数
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  lpuInstID：[OUT] 实例ID。P2P模块支持多实例，初始化时分配实例ID。
 *
 *  uMode：[IN] 运行模式：客户端或侦听端，见枚举“PG_MODE_E”
 *
 *  lpszUser：[IN] 客户端时为帐号用户名（侦听端时通常为设备ID）
 *
 *  lpszPass：[IN] 客户端时为帐号密码
 *
 *  lpszSvrAddr：[IN] P2P服务器的地址端口，例如：“127.0.0.1:3333”
 * 
 *  lpszRelayList：[IN] 中继服务器地址列表，P2P无法穿透的情况下通过中继服务器转发。
 *      格式示例："type=0&load=0&addr=127.0.0.1:443;type=1&load=100&addr=192.168.0.1:8000"
 *      每个中继服务器有type、load和addr三个参数，多个中继服务器之间用分号‘;’隔开。
 * 
 *  lpstInitCfg：[IN] 初始化参数，见结构“PG_INIT_CFG_S”的定义。
 *
 *  pfnLogOut：[IN] 日志输出回调函数的指针。回调函数原型见‘TfnLogOut’定义。
 *
 *  返回值：见枚举‘PG_ERROR_E’的定义
 */
int pgInitialize(unsigned int* lpuInstID, unsigned int uMode,
	const char* lpszUser, const char* lpszPass, const char* lpszSvrAddr,
	const char* lpszRelayList, PG_INIT_CFG_S *lpstInitCfg, TfnLogOut pfnLogOut);


/**
 *  描述：P2P穿透模块清理，释放所有资源。
 * 
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 */
void pgCleanup(unsigned int uInstID);


/**
 *  描述：设置日志输出的级别。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uLevel：[IN] 日志输出级别：
 *          0：重要的日志信息（默认开启），
 *          1：数量较多但次要的日志信息（默认关闭）。
 *
 *  uEnable：[IN] 0：关闭，非0：开启。
 *
 *  返回值：等于0为成功，小于0为错误码
 */
int pgLevel(unsigned int uLevel, unsigned int uEnable);


/**
 *  描述：获取实例本端的P2P ID。
 * 
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpszSelfID：[OUT] 接受本端P2P ID的缓冲区。
 *
 *  uSize：[IN] ‘lpszSelfID’缓冲区的长度，不小于128字节。
 *
 *  返回值：见枚举‘PG_ERROR_E’的定义。
 */
int pgSelf(unsigned int uInstID, char* lpszSelfID, unsigned int uSize);


/**
 *  描述：等待底层事件的函数，可以同时等待多个会话上的多种事件。事件发生后函数返回。
 *
 *  阻塞方式：阻塞，有事件达到或等待超时后返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpuEventNow：[OUT] 输出当前发生的事件，见枚举‘PG_EVENT_E’定义
 *
 *  lpuSessIDNow：[OUT] 当前发生事件的会话ID。
 *      在‘PG_EVENT_SVR_XXX’事件时忽略此参数。
 *
 *  lpuPrio：[OUT] 输出当前发生事件的优先级，见枚举‘PG_PRIORITY_E’定义。
 *      在‘PG_EVENT_SVR_XXX’事件时忽略此参数。
 *
 *  uTimeout：[IN] 等待超时时间(毫秒)。传0为不等待，立即返回。
 *
 *  返回值：见枚举‘PG_ERROR_E’的定义。
 */
int pgEvent(unsigned int uInstID, unsigned int* lpuEventNow,
	unsigned int* lpuSessIDNow, unsigned int* lpuPrio, unsigned int uTimeout);


/**
 *  描述：创建一个会话，并同时向侦听端发起连接请求（只能在客户端模式调用）。
 *
 *  阻塞方式：非阻塞，立即返回。
 *      连接不会马上完成，等pgEvent()收到PG_EVENT_CONNECT事件后，连接才成功。
 *      如果pgEvent()收到PG_EVENT_CLOSE事件，说明连接无法成功，需要调用pgClose()关闭会话。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpszListenID：[IN] 侦听端的ID，也就是侦听端在pgInitialize()中传入的‘lpszUser’用户名。
 *
 *  lpuSessID：[OUT] 输出会话ID
 *
 *  返回值：见枚举‘PG_ERROR_E’的定义。
 */
int pgOpen(unsigned int uInstID, const char* lpszListenID, unsigned int* lpuSessID);


/**
 *  描述：在一个会话上发送数据
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  uSessID：[IN] 会话ID
 *
 *  lpvData：[IN] 数据缓冲区地址
 *
 *  uDataLen：[IN] 数据长度。
 *      为了提高性能，建议一次发送较长的数据，P2P模块内部会将数据进行分片传输，在接收端再把数据组合还原。
 *      但一次发送的数据长度不能超过P2P模快内部的发送缓冲区的长度，参考PG_INIT_CFG_S结构的uBufSize属性的说明。
 *
 *  uPriority：[IN] 数据优先级，见枚举’PG_PRIORITY_E’定义。
 *
 *  返回值：大于0为发送的数据长度。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgWrite(unsigned int uInstID, unsigned int uSessID,
	const void* lpvData, unsigned int uDataLen, unsigned int uPriority);


/**
 *  描述：在一个会话上接收数据
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  uSessID：[IN] 会话ID
 *
 *  lpvBuf：[OUT] 接收缓冲区地址
 *
 *  uBufLen：[IN] 缓冲区长度。
 *      pgRead()一次接收的数据等于pgWrite()一次发送的数据。
 *      如果pgWrite()一次发送的数据很长（例如16K字节），则P2P模块自动把数据分片进行传输。
 *      在接收端，P2P模块把分片的数据重新组合还原，让pgRead()一次读出pgWrite()一次发送的数据。
 *
 *  lpuPriority：[OUT] 数据优先级，见枚举’PG_PRIORITY_E’定义。
 *
 *  返回值：大于0为接收的数据长度。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgRead(unsigned int uInstID, unsigned int uSessID,
	void* lpvBuf, unsigned int uBufLen, unsigned int* lpuPriority);


/**
 *  描述：获取本端地址信息或一个会话对端的地址信息。
 *        需要在收到PG_EVENT_INFO事件以后，才能成功获取有效信息。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  uSessID：[IN] 会话ID。uSessID为0时，获取本端的信息。uSessID为非零0时，获取对端的信息。
 *
 *  lpstInfo：[OUT] 获取到的会话信息，见‘PG_INFO_S’结构定义
 *
 *  返回值：0为成功。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgInfo(unsigned int uInstID, unsigned int uSessID, PG_INFO_S* lpstInfo);


/**
 *  描述：获取一个会话的发送缓冲区剩余的帧数。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  uSessID：[IN] 会话ID
 *
 *  uPriority：[IN] 数据优先级，见枚举’PG_PRIORITY_E’定义。
 *
 *  返回值：大于等于0为发送缓冲区剩余的帧数。小于0为错误码
 */
int pgPend(unsigned int uInstID, unsigned int uSessID, unsigned int uPriority);


/**
 *  描述：查看当前需要接收的数据的长度和优先级。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  uSessID：[IN] 会话ID
 *
 *  lpuPriority [OUT] 当前需要接收的数据的优先级，见枚举’PG_PRIORITY_E’定义。
 *
 *  返回值：大于等于0为当前需要接收的数据的长度，小于0为错误码
 */
int pgPeek(unsigned int uInstID, unsigned int uSessID, unsigned int* lpuPriority);


/**
 *  描述：判断一个会话是否已经连接完成。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  uSessID：[IN] 会话ID
 *
 *  返回值：0为已经连接。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgConnected(unsigned int uInstID, unsigned int uSessID);


/**
 *  描述：关闭一个会话。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  uSessID：[IN] 会话ID
 */
void pgClose(unsigned int uInstID, unsigned int uSessID);


/**
 *  描述：向P2P服务器发送一个请求。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpszData：[IN] 请求发送的内容，任意字符串。
 *
 *  uParam：[IN] 自定义参数。（目的是使请求和应答能够相匹配）。
 *
 *  返回值：0为成功。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgServerRequest(unsigned int uInstID, const char* lpszData, unsigned int uParam);


/**
 *  描述：接收P2P服务器的应答。
 *      pgEvent()函数收到‘PG_EVENT_SVR_REPLY’事件时，调用此函数接收应答内容。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpszData：[OUT] 接收应答内容的缓冲区，输出P2P服务器返回的字符串。
 *
 *  uSize：[IN] 接收缓冲区的长度
 *
 *  lpuParam：[OUT] 返回在pgServerRequest()函数输入的自定义参数
 *
 *  返回值：大于等于0为接收缓冲区中的应答内容长度。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgServerReply(unsigned int uInstID, char* lpszData, unsigned int uSize, unsigned int* lpuParam);


/**
 *  描述：接收P2P服务器的主动推送。
 *      pgEvent()函数收到‘PG_EVENT_SVR_NOTIFY’事件时，调用此函数接收推送内容。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpszData：[OUT] 接收推送内容的缓冲区，输出P2P服务器推送的字符串。
 *
 *  uSize：[IN] 接收缓冲区的长度
 *
 *  返回值：大于等于0为接收缓冲区中的推送内容长度。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgServerNotify(unsigned int uInstID, char* lpszData, unsigned int uSize);


/**
 *  描述：指定连接P2P服务器的网络方式。
 *      在手机系统上使用P2P时，如果手机休眠，则网络切换到
 *      “只使用Relay转发(PG_NET_MODE_Relay)”方式连接，可增强手机APP在休眠状态下的在线能力。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  uMode：[IN] 网络连接方式，见‘PG_NET_MODE_E’枚举。
 *
 *  返回值：0为成功。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgServerNetMode(unsigned int uInstID, unsigned int uMode);


/**
 *  描述：发起局域网内的P2P节点搜索。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  返回值：0为成功。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgLanScanStart(unsigned int uInstID);


/**
 *  描述：获取局域网P2P节点搜索的结果。
 *        在上报PG_EVENT_LAN_SCAN事件之后调用。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpstLanList：[OUT] 接收局域网P2P节点信息的数组。
 *
 *  uSize：[IN] 接收缓冲区的长度
 *
 *  返回值：大于等于0为返回的P2P节点个数。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgLanScanResult(unsigned int uInstID, PG_LAN_SCAN_S* lpstLanList, unsigned int uSize);


/**
 *  描述：获取本模块的版本。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  lpszVersion：[OUT] 接受版本信息的缓冲区。
 *
 *  uSize：[IN] 缓冲区的长度（建议大于等于16字节）
 */
void pgVersion(char* lpszVersion, unsigned int uSize);


/**
 *  描述：触发立即尝试登录到P2P服务器。
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  uDelay：[IN] 延时（秒）
 *
 *  返回值：0为成功。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgLoginNow(unsigned int uInstID, unsigned int uDelay);


/**
 *  描述：获取节点转发的状态信息
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpuStatus：[OUT] 当前的状态，（见枚举‘PG_PEER_FWD_STA_E’的定义）
 *  lpuSessCount：[OUT] 当前的协助转发会话数
 *
 *  返回值：0为成功。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgPeerFwdStatus(unsigned int uInstID, unsigned int* lpuStatus, unsigned int* lpuSessCount);


/**
 *  描述：获取节点转发的（贡献）统计信息
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpuSpeed：[OUT] 当前贡献的转发速度（字节/秒）
 *  lpuByteCount：[OUT] 从上一次调用此函数以来，已经贡献的协助转发的流量（字节数）
 *
 *  返回值：0为成功。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgPeerFwdStatistic(unsigned int uInstID, unsigned int* lpuSpeed, unsigned int* lpuByteCount);


/**
 *  描述：获取节点转发的（消费）统计信息
 *
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  lpuSendSpeed：[OUT] 当前消费的发送方向的转发速度（字节/秒）
 *  lpuSendByteCount：[OUT] 从上一次调用此函数以来，已经消费的发送方向的转发流量（字节数）
 *  lpuRecvSpeed：[OUT] 当前消费的接收方向的转发速度（字节/秒）
 *  lpuRecvByteCount：[OUT] 从上一次调用此函数以来，已经消费的接收方向的转发流量（字节数）
 *
 *  返回值：0为成功。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgPeerFwdStatUsed(unsigned int uInstID, unsigned int* lpuSendSpeed,
	unsigned int* lpuSendByteCount, unsigned int* lpuRecvSpeed, unsigned int* lpuRecvByteCount);


/**
 *  描述：动态设置节点转发的参数
 * 
 *  阻塞方式：非阻塞，立即返回。
 *
 *  uInstID：[IN] 实例ID，调用pgInitialize()时输出。
 *
 *  iSpeed: [IN] 允许本节点作为转发节点。
 *      小于0：不改变该配置参数，大于0：允许转发的带宽(字节/秒)，0：禁用转发。
 *
 *  iGate: [IN] 本节点触发停止协助转发的自用带宽的阈值(字节/秒)。
 *      小于0：不改变该配置参数，大于或等于0：指定阈值。默认：262144
 *
 *  iUse: [IN] 本节点是否使用其他节点作为转发节点。
 *      小于0：不改变该配置参数，大于0：是，0：否。默认：1。
 *
 *  iMaxSess: [IN] 本节点协助转发最大会话数。
 *      小于0：不改变该配置参数，大于或等于0：指定最大会话数。默认：3。
 *
 *  lpszOption: [IN] 选项，预留给以后使用。
 *
 *  返回值：0为成功。小于0为错误码（见枚举‘PG_ERROR_E’的定义）
 */
int pgPeerFwdCfg(unsigned int uInstID, int iSpeed,
	int iGate, int iUse, int iMaxSess, const char* lpszOption);


#ifdef __cplusplus
}
#endif


#endif // _PG_LIB_CONNECT_H
