/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/p2p/base/p2ptransportchannel.h"

#include <set>
#include "webrtc/p2p/base/common.h"
#include "webrtc/p2p/base/relayport.h"  // For RELAY_PORT_TYPE.
#include "webrtc/p2p/base/stunport.h"  // For STUN_PORT_TYPE.
#include "webrtc/base/common.h"
#include "webrtc/base/crc32.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/stringencode.h"

namespace {

// messages for queuing up work for ourselves
enum {
  MSG_SORT = 1,
  MSG_PING,
  MSG_CHECK_RECEIVING
};

// When the socket is unwritable, we will use 10 Kbps (ignoring IP+UDP headers)
// for pinging.  When the socket is writable, we will use only 1 Kbps because
// we don't want to degrade the quality on a modem.  These numbers should work
// well on a 28.8K modem, which is the slowest connection on which the voice
// quality is reasonable at all.
static const uint32 PING_PACKET_SIZE = 60 * 8;
static const uint32 WRITABLE_DELAY = 1000 * PING_PACKET_SIZE / 1000;  // 480ms
static const uint32 UNWRITABLE_DELAY = 1000 * PING_PACKET_SIZE / 10000;  // 50ms

// If there is a current writable connection, then we will also try hard to
// make sure it is pinged at this rate.
static const uint32 MAX_CURRENT_WRITABLE_DELAY = 900;  // 2*WRITABLE_DELAY - bit

static const int MIN_CHECK_RECEIVING_DELAY = 50;  // ms

// The minimum improvement in RTT that justifies a switch.
static const double kMinImprovement = 10;

cricket::PortInterface::CandidateOrigin GetOrigin(cricket::PortInterface* port,
                                         cricket::PortInterface* origin_port) {
  if (!origin_port)
    return cricket::PortInterface::ORIGIN_MESSAGE;
  else if (port == origin_port)
    return cricket::PortInterface::ORIGIN_THIS_PORT;
  else
    return cricket::PortInterface::ORIGIN_OTHER_PORT;
}

// Compares two connections based only on static information about them.
int CompareConnectionCandidates(cricket::Connection* a,
                                cricket::Connection* b) {
  // Compare connection priority. Lower values get sorted last.
  if (a->priority() > b->priority())
    return 1;
  if (a->priority() < b->priority())
    return -1;

  // If we're still tied at this point, prefer a younger generation.
  return (a->remote_candidate().generation() + a->port()->generation()) -
         (b->remote_candidate().generation() + b->port()->generation());
}

// Compare two connections based on their connected state, writability and
// static preferences.
int CompareConnections(cricket::Connection *a, cricket::Connection *b) {
  // Sort based on write-state.  Better states have lower values.
  if (a->write_state() < b->write_state())
    return 1;
  if (a->write_state() > b->write_state())
    return -1;

  // WARNING: Some complexity here about TCP reconnecting.
  // When a TCP connection fails because of a TCP socket disconnecting, the
  // active side of the connection will attempt to reconnect for 5 seconds while
  // pretending to be writable (the connection is not set to the unwritable
  // state).  On the passive side, the connection also remains writable even
  // though it is disconnected, and a new connection is created when the active
  // side connects.  At that point, there are two TCP connections on the passive
  // side: 1. the old, disconnected one that is pretending to be writable, and
  // 2.  the new, connected one that is maybe not yet writable.  For purposes of
  // pruning, pinging, and selecting the best connection, we want to treat the
  // new connection as "better" than the old one.  We could add a method called
  // something like Connection::ImReallyBadEvenThoughImWritable, but that is
  // equivalent to the existing Connection::connected(), which we already have.
  // So, in code throughout this file, we'll check whether the connection is
  // connected() or not, and if it is not, treat it as "worse" than a connected
  // one, even though it's writable.  In the code below, we're doing so to make
  // sure we treat a new writable connection as better than an old disconnected
  // connection.

  // In the case where we reconnect TCP connections, the original best
  // connection is disconnected without changing to WRITE_TIMEOUT. In this case,
  // the new connection, when it becomes writable, should have higher priority.
  if (a->write_state() == cricket::Connection::STATE_WRITABLE &&
      b->write_state() == cricket::Connection::STATE_WRITABLE) {
    if (a->connected() && !b->connected()) {
      return 1;
    }
    if (!a->connected() && b->connected()) {
      return -1;
    }
  }

  // Compare the candidate information.
  return CompareConnectionCandidates(a, b);
}

// Wraps the comparison connection into a less than operator that puts higher
// priority writable connections first.
class ConnectionCompare {
 public:
  bool operator()(const cricket::Connection *ca,
                  const cricket::Connection *cb) {
    cricket::Connection* a = const_cast<cricket::Connection*>(ca);
    cricket::Connection* b = const_cast<cricket::Connection*>(cb);

    // The IceProtocol is initialized to ICEPROTO_HYBRID and can be updated to
    // GICE or RFC5245 when an answer SDP is set, or when a STUN message is
    // received. So the port receiving the STUN message may have a different
    // IceProtocol if the answer SDP is not set yet.
    ASSERT(a->port()->IceProtocol() == b->port()->IceProtocol() ||
           a->port()->IceProtocol() == cricket::ICEPROTO_HYBRID ||
           b->port()->IceProtocol() == cricket::ICEPROTO_HYBRID);

    // Compare first on writability and static preferences.
    int cmp = CompareConnections(a, b);
    if (cmp > 0)
      return true;
    if (cmp < 0)
      return false;

    // Otherwise, sort based on latency estimate.
    return a->rtt() < b->rtt();

    // Should we bother checking for the last connection that last received
    // data? It would help rendezvous on the connection that is also receiving
    // packets.
    //
    // TODO: Yes we should definitely do this.  The TCP protocol gains
    // efficiency by being used bidirectionally, as opposed to two separate
    // unidirectional streams.  This test should probably occur before
    // comparison of local prefs (assuming combined prefs are the same).  We
    // need to be careful though, not to bounce back and forth with both sides
    // trying to rendevous with the other.
  }
};

// Determines whether we should switch between two connections, based first on
// static preferences and then (if those are equal) on latency estimates.
bool ShouldSwitch(cricket::Connection* a_conn, cricket::Connection* b_conn) {
  if (a_conn == b_conn)
    return false;

  if (!a_conn || !b_conn)  // don't think the latter should happen
    return true;

  int prefs_cmp = CompareConnections(a_conn, b_conn);
  if (prefs_cmp < 0)
    return true;
  if (prefs_cmp > 0)
    return false;

  return b_conn->rtt() <= a_conn->rtt() + kMinImprovement;
}

}  // unnamed namespace

namespace cricket {

P2PTransportChannel::P2PTransportChannel(const std::string& content_name,
                                         int component,
                                         P2PTransport* transport,
                                         PortAllocator *allocator) :
    TransportChannelImpl(content_name, component),
    transport_(transport),
    allocator_(allocator),
    worker_thread_(rtc::Thread::Current()),
    incoming_only_(false),
    waiting_for_signaling_(false),
    error_(0),
    best_connection_(NULL),
    pending_best_connection_(NULL),
    sort_dirty_(false),
    was_writable_(false),
    protocol_type_(ICEPROTO_HYBRID),
    remote_ice_mode_(ICEMODE_FULL),
    ice_role_(ICEROLE_UNKNOWN),
    tiebreaker_(0),
    remote_candidate_generation_(0),
    check_receiving_delay_(MIN_CHECK_RECEIVING_DELAY * 5),
    receiving_timeout_(MIN_CHECK_RECEIVING_DELAY * 50) {
}

P2PTransportChannel::~P2PTransportChannel() {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  for (uint32 i = 0; i < allocator_sessions_.size(); ++i)
    delete allocator_sessions_[i];
}

// Add the allocator session to our list so that we know which sessions
// are still active.
void P2PTransportChannel::AddAllocatorSession(PortAllocatorSession* session) {
  session->set_generation(static_cast<uint32>(allocator_sessions_.size()));
  allocator_sessions_.push_back(session);

  // We now only want to apply new candidates that we receive to the ports
  // created by this new session because these are replacing those of the
  // previous sessions.
  ports_.clear();

  session->SignalPortReady.connect(this, &P2PTransportChannel::OnPortReady);
  session->SignalCandidatesReady.connect(
      this, &P2PTransportChannel::OnCandidatesReady);
  session->SignalCandidatesAllocationDone.connect(
      this, &P2PTransportChannel::OnCandidatesAllocationDone);
  session->StartGettingPorts();
}

void P2PTransportChannel::AddConnection(Connection* connection) {
  connections_.push_back(connection);
  connection->set_remote_ice_mode(remote_ice_mode_);
  connection->SignalReadPacket.connect(
      this, &P2PTransportChannel::OnReadPacket);
  connection->SignalReadyToSend.connect(
      this, &P2PTransportChannel::OnReadyToSend);
  connection->SignalStateChange.connect(
      this, &P2PTransportChannel::OnConnectionStateChange);
  connection->SignalDestroyed.connect(
      this, &P2PTransportChannel::OnConnectionDestroyed);
  connection->SignalNominated.connect(this, &P2PTransportChannel::OnNominated);
}

void P2PTransportChannel::SetIceRole(IceRole ice_role) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  if (ice_role_ != ice_role) {
    ice_role_ = ice_role;
    for (std::vector<PortInterface *>::iterator it = ports_.begin();
         it != ports_.end(); ++it) {
      (*it)->SetIceRole(ice_role);
    }
  }
}

void P2PTransportChannel::SetIceTiebreaker(uint64 tiebreaker) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  if (!ports_.empty()) {
    LOG(LS_ERROR)
        << "Attempt to change tiebreaker after Port has been allocated.";
    return;
  }

  tiebreaker_ = tiebreaker;
}

// Currently a channel is considered ICE completed once there is no
// more than one connection per Network. This works for a single NIC
// with both IPv4 and IPv6 enabled. However, this condition won't
// happen when there are multiple NICs and all of them have
// connectivity.
// TODO(guoweis): Change Completion to be driven by a channel level
// timer.
TransportChannelState P2PTransportChannel::GetState() const {
  std::set<rtc::Network*> networks;

  if (connections_.size() == 0) {
    return TransportChannelState::STATE_FAILED;
  }

  for (uint32 i = 0; i < connections_.size(); ++i) {
    rtc::Network* network = connections_[i]->port()->Network();
    if (networks.find(network) == networks.end()) {
      networks.insert(network);
    } else {
      LOG_J(LS_VERBOSE, this) << "Ice not completed yet for this channel as "
                              << network->ToString()
                              << " has more than 1 connection.";
      return TransportChannelState::STATE_CONNECTING;
    }
  }
  LOG_J(LS_VERBOSE, this) << "Ice is completed for this channel.";

  return TransportChannelState::STATE_COMPLETED;
}

bool P2PTransportChannel::GetIceProtocolType(IceProtocolType* type) const {
  *type = protocol_type_;
  return true;
}

void P2PTransportChannel::SetIceProtocolType(IceProtocolType type) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  protocol_type_ = type;
  for (std::vector<PortInterface *>::iterator it = ports_.begin();
       it != ports_.end(); ++it) {
    (*it)->SetIceProtocolType(protocol_type_);
  }
}

void P2PTransportChannel::SetIceCredentials(const std::string& ice_ufrag,
                                            const std::string& ice_pwd) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  bool ice_restart = false;
  if (!ice_ufrag_.empty() && !ice_pwd_.empty()) {
    // Restart candidate allocation if there is any change in either
    // ice ufrag or password.
    ice_restart =
        IceCredentialsChanged(ice_ufrag_, ice_pwd_, ice_ufrag, ice_pwd);
  }

  ice_ufrag_ = ice_ufrag;
  ice_pwd_ = ice_pwd;

  if (ice_restart) {
    // Restart candidate gathering.
    Allocate();
  }
}

void P2PTransportChannel::SetRemoteIceCredentials(const std::string& ice_ufrag,
                                                  const std::string& ice_pwd) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  bool ice_restart = false;
  if (!remote_ice_ufrag_.empty() && !remote_ice_pwd_.empty()) {
    ice_restart = (remote_ice_ufrag_ != ice_ufrag) ||
                  (remote_ice_pwd_!= ice_pwd);
  }

  remote_ice_ufrag_ = ice_ufrag;
  remote_ice_pwd_ = ice_pwd;

  // We need to update the credentials for any peer reflexive candidates.
  std::vector<Connection*>::iterator it = connections_.begin();
  for (; it != connections_.end(); ++it) {
    (*it)->MaybeSetRemoteIceCredentials(ice_ufrag, ice_pwd);
  }

  if (ice_restart) {
    // |candidate.generation()| is not signaled in ICEPROTO_RFC5245.
    // Therefore we need to keep track of the remote ice restart so
    // newer connections are prioritized over the older.
    ++remote_candidate_generation_;
  }
}

void P2PTransportChannel::SetRemoteIceMode(IceMode mode) {
  remote_ice_mode_ = mode;
}

void P2PTransportChannel::SetReceivingTimeout(int receiving_timeout_ms) {
  if (receiving_timeout_ms < 0) {
    return;
  }
  receiving_timeout_ = receiving_timeout_ms;
  check_receiving_delay_ =
      std::max(MIN_CHECK_RECEIVING_DELAY, receiving_timeout_ / 10);
  LOG(LS_VERBOSE) << "Set ICE receiving timeout to " << receiving_timeout_
                  << " milliseconds";
}

// Go into the state of processing candidates, and running in general
void P2PTransportChannel::Connect() {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  if (ice_ufrag_.empty() || ice_pwd_.empty()) {
    ASSERT(false);
    LOG(LS_ERROR) << "P2PTransportChannel::Connect: The ice_ufrag_ and the "
                  << "ice_pwd_ are not set.";
    return;
  }

  // Kick off an allocator session
  Allocate();

  // Start pinging as the ports come in.
  thread()->Post(this, MSG_PING);

  thread()->PostDelayed(
      check_receiving_delay_, this, MSG_CHECK_RECEIVING);
}

// A new port is available, attempt to make connections for it
void P2PTransportChannel::OnPortReady(PortAllocatorSession *session,
                                      PortInterface* port) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  // Set in-effect options on the new port
  for (OptionMap::const_iterator it = options_.begin();
       it != options_.end();
       ++it) {
    int val = port->SetOption(it->first, it->second);
    if (val < 0) {
      LOG_J(LS_WARNING, port) << "SetOption(" << it->first
                              << ", " << it->second
                              << ") failed: " << port->GetError();
    }
  }

  // Remember the ports and candidates, and signal that candidates are ready.
  // The session will handle this, and send an initiate/accept/modify message
  // if one is pending.

  port->SetIceProtocolType(protocol_type_);
  port->SetIceRole(ice_role_);
  port->SetIceTiebreaker(tiebreaker_);
  ports_.push_back(port);
  port->SignalUnknownAddress.connect(
      this, &P2PTransportChannel::OnUnknownAddress);
  port->SignalDestroyed.connect(this, &P2PTransportChannel::OnPortDestroyed);
  port->SignalRoleConflict.connect(
      this, &P2PTransportChannel::OnRoleConflict);

  // Attempt to create a connection from this new port to all of the remote
  // candidates that we were given so far.

  std::vector<RemoteCandidate>::iterator iter;
  for (iter = remote_candidates_.begin(); iter != remote_candidates_.end();
       ++iter) {
    CreateConnection(port, *iter, iter->origin_port(), false);
  }

  SortConnections();
}

// A new candidate is available, let listeners know
void P2PTransportChannel::OnCandidatesReady(
    PortAllocatorSession *session, const std::vector<Candidate>& candidates) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  for (size_t i = 0; i < candidates.size(); ++i) {
    SignalCandidateReady(this, candidates[i]);
  }
}

void P2PTransportChannel::OnCandidatesAllocationDone(
    PortAllocatorSession* session) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  SignalCandidatesAllocationDone(this);
}

// Handle stun packets
void P2PTransportChannel::OnUnknownAddress(
    PortInterface* port,
    const rtc::SocketAddress& address, ProtocolType proto,
    IceMessage* stun_msg, const std::string &remote_username,
    bool port_muxed) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  // Port has received a valid stun packet from an address that no Connection
  // is currently available for. See if we already have a candidate with the
  // address. If it isn't we need to create new candidate for it.

  // Determine if the remote candidates use shared ufrag.
  bool ufrag_per_port = false;
  std::vector<RemoteCandidate>::iterator it;
  if (remote_candidates_.size() > 0) {
    it = remote_candidates_.begin();
    std::string username = it->username();
    for (; it != remote_candidates_.end(); ++it) {
      if (it->username() != username) {
        ufrag_per_port = true;
        break;
      }
    }
  }

  const Candidate* candidate = NULL;
  std::string remote_password;
  for (it = remote_candidates_.begin(); it != remote_candidates_.end(); ++it) {
    if (it->username() == remote_username) {
      remote_password = it->password();
      if (ufrag_per_port ||
          (it->address() == address &&
           it->protocol() == ProtoToString(proto))) {
        candidate = &(*it);
        break;
      }
      // We don't want to break here because we may find a match of the address
      // later.
    }
  }

  // The STUN binding request may arrive after setRemoteDescription and before
  // adding remote candidate, so we need to set the password to the shared
  // password if the user name matches.
  if (remote_password.empty() && remote_username == remote_ice_ufrag_) {
    remote_password = remote_ice_pwd_;
  }

  Candidate remote_candidate;
  bool remote_candidate_is_new = (candidate == nullptr);
  if (!remote_candidate_is_new) {
    remote_candidate = *candidate;
    if (ufrag_per_port) {
      remote_candidate.set_address(address);
    }
  } else {
    // Create a new candidate with this address.
    std::string type;
    int remote_candidate_priority;
    if (port->IceProtocol() == ICEPROTO_RFC5245) {
      // RFC 5245
      // If the source transport address of the request does not match any
      // existing remote candidates, it represents a new peer reflexive remote
      // candidate.
      type = PRFLX_PORT_TYPE;

      // The priority of the candidate is set to the PRIORITY attribute
      // from the request.
      const StunUInt32Attribute* priority_attr =
          stun_msg->GetUInt32(STUN_ATTR_PRIORITY);
      if (!priority_attr) {
        LOG(LS_WARNING) << "P2PTransportChannel::OnUnknownAddress - "
                        << "No STUN_ATTR_PRIORITY found in the "
                        << "stun request message";
        port->SendBindingErrorResponse(stun_msg, address,
                                       STUN_ERROR_BAD_REQUEST,
                                       STUN_ERROR_REASON_BAD_REQUEST);
        return;
      }
      remote_candidate_priority = priority_attr->value();
    } else {
      // G-ICE doesn't support prflx candidate.
      // We set candidate type to STUN_PORT_TYPE if the binding request comes
      // from a relay port or the shared socket is used. Otherwise we use the
      // port's type as the candidate type.
      if (port->Type() == RELAY_PORT_TYPE || port->SharedSocket()) {
        type = STUN_PORT_TYPE;
      } else {
        type = port->Type();
      }
      remote_candidate_priority = remote_candidate.GetPriority(
          ICE_TYPE_PREFERENCE_PRFLX, port->Network()->preference(), 0);
    }

    remote_candidate =
        Candidate(component(), ProtoToString(proto), address, 0,
                  remote_username, remote_password, type, 0U, "");

    // From RFC 5245, section-7.2.1.3:
    // The foundation of the candidate is set to an arbitrary value, different
    // from the foundation for all other remote candidates.
    remote_candidate.set_foundation(
        rtc::ToString<uint32>(rtc::ComputeCrc32(remote_candidate.id())));

    remote_candidate.set_priority(remote_candidate_priority);
  }

  if (port->IceProtocol() == ICEPROTO_RFC5245) {
    // RFC5245, the agent constructs a pair whose local candidate is equal to
    // the transport address on which the STUN request was received, and a
    // remote candidate equal to the source transport address where the
    // request came from.

    // There shouldn't be an existing connection with this remote address.
    // When ports are muxed, this channel might get multiple unknown address
    // signals. In that case if the connection is already exists, we should
    // simply ignore the signal otherwise send server error.
    if (port->GetConnection(remote_candidate.address())) {
      if (port_muxed) {
        LOG(LS_INFO) << "Connection already exists for peer reflexive "
                     << "candidate: " << remote_candidate.ToString();
        return;
      } else {
        ASSERT(false);
        port->SendBindingErrorResponse(stun_msg, address,
                                       STUN_ERROR_SERVER_ERROR,
                                       STUN_ERROR_REASON_SERVER_ERROR);
        return;
      }
    }

    Connection* connection = port->CreateConnection(
        remote_candidate, cricket::PortInterface::ORIGIN_THIS_PORT);
    if (!connection) {
      ASSERT(false);
      port->SendBindingErrorResponse(stun_msg, address,
                                     STUN_ERROR_SERVER_ERROR,
                                     STUN_ERROR_REASON_SERVER_ERROR);
      return;
    }

    LOG(LS_INFO) << "Adding connection from "
                 << (remote_candidate_is_new ? "peer reflexive" : "resurrected")
                 << " candidate: " << remote_candidate.ToString();
    AddConnection(connection);
    connection->ReceivedPing();

    // Send the pinger a successful stun response.
    port->SendBindingResponse(stun_msg, address);

    bool received_use_candidate =
        stun_msg->GetByteString(STUN_ATTR_USE_CANDIDATE) != nullptr;
    if (received_use_candidate && ice_role_ == ICEROLE_CONTROLLED) {
      connection->set_nominated(true);
      OnNominated(connection);
    }

    // Update the list of connections since we just added another.  We do this
    // after sending the response since it could (in principle) delete the
    // connection in question.
    SortConnections();
  } else {
    // Check for connectivity to this address. Create connections
    // to this address across all local ports. First, add this as a new remote
    // address
    if (!CreateConnections(remote_candidate, port, true)) {
      // Hopefully this won't occur, because changing a destination address
      // shouldn't cause a new connection to fail
      ASSERT(false);
      port->SendBindingErrorResponse(stun_msg, address, STUN_ERROR_SERVER_ERROR,
          STUN_ERROR_REASON_SERVER_ERROR);
      return;
    }

    // Send the pinger a successful stun response.
    port->SendBindingResponse(stun_msg, address);

    // Update the list of connections since we just added another.  We do this
    // after sending the response since it could (in principle) delete the
    // connection in question.
    SortConnections();
  }
}

void P2PTransportChannel::OnRoleConflict(PortInterface* port) {
  SignalRoleConflict(this);  // STUN ping will be sent when SetRole is called
                             // from Transport.
}

// When the signalling channel is ready, we can really kick off the allocator
void P2PTransportChannel::OnSignalingReady() {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  if (waiting_for_signaling_) {
    waiting_for_signaling_ = false;
    AddAllocatorSession(allocator_->CreateSession(
        SessionId(), content_name(), component(), ice_ufrag_, ice_pwd_));
  }
}

void P2PTransportChannel::OnNominated(Connection* conn) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  ASSERT(ice_role_ == ICEROLE_CONTROLLED);
  ASSERT(protocol_type_ == ICEPROTO_RFC5245);

  if (conn->write_state() == Connection::STATE_WRITABLE) {
    if (best_connection_ != conn) {
      pending_best_connection_ = NULL;
      LOG(LS_INFO) << "Switching best connection on controlled side: "
                   << conn->ToString();
      SwitchBestConnectionTo(conn);
      // Now we have selected the best connection, time to prune other existing
      // connections and update the read/write state of the channel.
      RequestSort();
    }
  } else {
    LOG(LS_INFO) << "Not switching the best connection on controlled side yet,"
                 << " because it's not writable: " << conn->ToString();
    pending_best_connection_ = conn;
  }
}

void P2PTransportChannel::OnCandidate(const Candidate& candidate) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  uint32 generation = candidate.generation();
  // Network may not guarantee the order of the candidate delivery. If a
  // remote candidate with an older generation arrives, drop it.
  if (generation != 0 && generation < remote_candidate_generation_) {
    LOG(LS_WARNING) << "Dropping a remote candidate because its generation "
                    << generation
                    << " is lower than the current remote generation "
                    << remote_candidate_generation_;
    return;
  }

  // Create connections to this remote candidate.
  CreateConnections(candidate, NULL, false);

  // Resort the connections list, which may have new elements.
  SortConnections();
}

// Creates connections from all of the ports that we care about to the given
// remote candidate.  The return value is true if we created a connection from
// the origin port.
bool P2PTransportChannel::CreateConnections(const Candidate& remote_candidate,
                                            PortInterface* origin_port,
                                            bool readable) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  Candidate new_remote_candidate(remote_candidate);
  new_remote_candidate.set_generation(
      GetRemoteCandidateGeneration(remote_candidate));
  // ICE candidates don't need to have username and password set, but
  // the code below this (specifically, ConnectionRequest::Prepare in
  // port.cc) uses the remote candidates's username.  So, we set it
  // here.
  if (remote_candidate.username().empty()) {
    new_remote_candidate.set_username(remote_ice_ufrag_);
  }
  if (remote_candidate.password().empty()) {
    new_remote_candidate.set_password(remote_ice_pwd_);
  }

  // If we've already seen the new remote candidate (in the current candidate
  // generation), then we shouldn't try creating connections for it.
  // We either already have a connection for it, or we previously created one
  // and then later pruned it. If we don't return, the channel will again
  // re-create any connections that were previously pruned, which will then
  // immediately be re-pruned, churning the network for no purpose.
  // This only applies to candidates received over signaling (i.e. origin_port
  // is NULL).
  if (!origin_port && IsDuplicateRemoteCandidate(new_remote_candidate)) {
    // return true to indicate success, without creating any new connections.
    return true;
  }

  // Add a new connection for this candidate to every port that allows such a
  // connection (i.e., if they have compatible protocols) and that does not
  // already have a connection to an equivalent candidate.  We must be careful
  // to make sure that the origin port is included, even if it was pruned,
  // since that may be the only port that can create this connection.
  bool created = false;
  std::vector<PortInterface *>::reverse_iterator it;
  for (it = ports_.rbegin(); it != ports_.rend(); ++it) {
    if (CreateConnection(*it, new_remote_candidate, origin_port, readable)) {
      if (*it == origin_port)
        created = true;
    }
  }

  if ((origin_port != NULL) &&
      std::find(ports_.begin(), ports_.end(), origin_port) == ports_.end()) {
    if (CreateConnection(
        origin_port, new_remote_candidate, origin_port, readable))
      created = true;
  }

  // Remember this remote candidate so that we can add it to future ports.
  RememberRemoteCandidate(new_remote_candidate, origin_port);

  return created;
}

// Setup a connection object for the local and remote candidate combination.
// And then listen to connection object for changes.
bool P2PTransportChannel::CreateConnection(PortInterface* port,
                                           const Candidate& remote_candidate,
                                           PortInterface* origin_port,
                                           bool readable) {
  // Look for an existing connection with this remote address.  If one is not
  // found, then we can create a new connection for this address.
  Connection* connection = port->GetConnection(remote_candidate.address());
  if (connection != NULL) {
    connection->MaybeUpdatePeerReflexiveCandidate(remote_candidate);

    // It is not legal to try to change any of the parameters of an existing
    // connection; however, the other side can send a duplicate candidate.
    if (!remote_candidate.IsEquivalent(connection->remote_candidate())) {
      LOG(INFO) << "Attempt to change a remote candidate."
                << " Existing remote candidate: "
                << connection->remote_candidate().ToString()
                << "New remote candidate: "
                << remote_candidate.ToString();
      return false;
    }
  } else {
    PortInterface::CandidateOrigin origin = GetOrigin(port, origin_port);

    // Don't create connection if this is a candidate we received in a
    // message and we are not allowed to make outgoing connections.
    if (origin == cricket::PortInterface::ORIGIN_MESSAGE && incoming_only_)
      return false;

    connection = port->CreateConnection(remote_candidate, origin);
    if (!connection)
      return false;

    AddConnection(connection);

    LOG_J(LS_INFO, this) << "Created connection with origin=" << origin << ", ("
                         << connections_.size() << " total)";
  }

  // If we are readable, it is because we are creating this in response to a
  // ping from the other side.  This will cause the state to become readable.
  if (readable)
    connection->ReceivedPing();

  return true;
}

bool P2PTransportChannel::FindConnection(
    cricket::Connection* connection) const {
  std::vector<Connection*>::const_iterator citer =
      std::find(connections_.begin(), connections_.end(), connection);
  return citer != connections_.end();
}

uint32 P2PTransportChannel::GetRemoteCandidateGeneration(
    const Candidate& candidate) {
  if (protocol_type_ == ICEPROTO_GOOGLE) {
    // The Candidate.generation() can be trusted. Nothing needs to be done.
    return candidate.generation();
  }
  // |candidate.generation()| is not signaled in ICEPROTO_RFC5245.
  // Therefore we need to keep track of the remote ice restart so
  // newer connections are prioritized over the older.
  ASSERT(candidate.generation() == 0 ||
         candidate.generation() == remote_candidate_generation_);
  return remote_candidate_generation_;
}

// Check if remote candidate is already cached.
bool P2PTransportChannel::IsDuplicateRemoteCandidate(
    const Candidate& candidate) {
  for (uint32 i = 0; i < remote_candidates_.size(); ++i) {
    if (remote_candidates_[i].IsEquivalent(candidate)) {
      return true;
    }
  }
  return false;
}

// Maintain our remote candidate list, adding this new remote one.
void P2PTransportChannel::RememberRemoteCandidate(
    const Candidate& remote_candidate, PortInterface* origin_port) {
  // Remove any candidates whose generation is older than this one.  The
  // presence of a new generation indicates that the old ones are not useful.
  uint32 i = 0;
  while (i < remote_candidates_.size()) {
    if (remote_candidates_[i].generation() < remote_candidate.generation()) {
      LOG(INFO) << "Pruning candidate from old generation: "
                << remote_candidates_[i].address().ToSensitiveString();
      remote_candidates_.erase(remote_candidates_.begin() + i);
    } else {
      i += 1;
    }
  }

  // Make sure this candidate is not a duplicate.
  if (IsDuplicateRemoteCandidate(remote_candidate)) {
    LOG(INFO) << "Duplicate candidate: " << remote_candidate.ToString();
    return;
  }

  // Try this candidate for all future ports.
  remote_candidates_.push_back(RemoteCandidate(remote_candidate, origin_port));
}

// Set options on ourselves is simply setting options on all of our available
// port objects.
int P2PTransportChannel::SetOption(rtc::Socket::Option opt, int value) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  OptionMap::iterator it = options_.find(opt);
  if (it == options_.end()) {
    options_.insert(std::make_pair(opt, value));
  } else if (it->second == value) {
    return 0;
  } else {
    it->second = value;
  }

  for (uint32 i = 0; i < ports_.size(); ++i) {
    int val = ports_[i]->SetOption(opt, value);
    if (val < 0) {
      // Because this also occurs deferred, probably no point in reporting an
      // error
      LOG(WARNING) << "SetOption(" << opt << ", " << value << ") failed: "
                   << ports_[i]->GetError();
    }
  }
  return 0;
}

bool P2PTransportChannel::GetOption(rtc::Socket::Option opt, int* value) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  const auto& found = options_.find(opt);
  if (found == options_.end()) {
    return false;
  }
  *value = found->second;
  return true;
}

// Send data to the other side, using our best connection.
int P2PTransportChannel::SendPacket(const char *data, size_t len,
                                    const rtc::PacketOptions& options,
                                    int flags) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  if (flags != 0) {
    error_ = EINVAL;
    return -1;
  }
  if (best_connection_ == NULL) {
    error_ = EWOULDBLOCK;
    return -1;
  }

  int sent = best_connection_->Send(data, len, options);
  if (sent <= 0) {
    ASSERT(sent < 0);
    error_ = best_connection_->GetError();
  }
  return sent;
}

bool P2PTransportChannel::GetStats(ConnectionInfos *infos) {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  // Gather connection infos.
  infos->clear();

  std::vector<Connection *>::const_iterator it;
  for (it = connections_.begin(); it != connections_.end(); ++it) {
    Connection *connection = *it;
    ConnectionInfo info;
    info.best_connection = (best_connection_ == connection);
    info.readable =
        (connection->read_state() == Connection::STATE_READABLE);
    info.writable =
        (connection->write_state() == Connection::STATE_WRITABLE);
    info.timeout =
        (connection->write_state() == Connection::STATE_WRITE_TIMEOUT);
    info.new_connection = !connection->reported();
    connection->set_reported(true);
    info.rtt = connection->rtt();
    info.sent_total_bytes = connection->sent_total_bytes();
    info.sent_bytes_second = connection->sent_bytes_second();
    info.sent_discarded_packets = connection->sent_discarded_packets();
    info.sent_total_packets = connection->sent_total_packets();
    info.recv_total_bytes = connection->recv_total_bytes();
    info.recv_bytes_second = connection->recv_bytes_second();
    info.local_candidate = connection->local_candidate();
    info.remote_candidate = connection->remote_candidate();
    info.key = connection;
    infos->push_back(info);
  }

  return true;
}

rtc::DiffServCodePoint P2PTransportChannel::DefaultDscpValue() const {
  OptionMap::const_iterator it = options_.find(rtc::Socket::OPT_DSCP);
  if (it == options_.end()) {
    return rtc::DSCP_NO_CHANGE;
  }
  return static_cast<rtc::DiffServCodePoint> (it->second);
}

// Begin allocate (or immediately re-allocate, if MSG_ALLOCATE pending)
void P2PTransportChannel::Allocate() {
  // Time for a new allocator, lets make sure we have a signalling channel
  // to communicate candidates through first.
  waiting_for_signaling_ = true;
  SignalRequestSignaling(this);
}

// Monitor connection states.
void P2PTransportChannel::UpdateConnectionStates() {
  uint32 now = rtc::Time();

  // We need to copy the list of connections since some may delete themselves
  // when we call UpdateState.
  for (uint32 i = 0; i < connections_.size(); ++i)
    connections_[i]->UpdateState(now);
}

// Prepare for best candidate sorting.
void P2PTransportChannel::RequestSort() {
  if (!sort_dirty_) {
    worker_thread_->Post(this, MSG_SORT);
    sort_dirty_ = true;
  }
}

// Sort the available connections to find the best one.  We also monitor
// the number of available connections and the current state.
void P2PTransportChannel::SortConnections() {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  // Make sure the connection states are up-to-date since this affects how they
  // will be sorted.
  UpdateConnectionStates();

  if (protocol_type_ == ICEPROTO_HYBRID) {
    // If we are in hybrid mode, we are not sending any ping requests, so there
    // is no point in sorting the connections. In hybrid state, ports can have
    // different protocol than hybrid and protocol may differ from one another.
    // Instead just update the state of this channel
    UpdateChannelState();
    return;
  }

  // Any changes after this point will require a re-sort.
  sort_dirty_ = false;

  // Find the best alternative connection by sorting.  It is important to note
  // that amongst equal preference, writable connections, this will choose the
  // one whose estimated latency is lowest.  So it is the only one that we
  // need to consider switching to.
  ConnectionCompare cmp;
  std::stable_sort(connections_.begin(), connections_.end(), cmp);
  LOG(LS_VERBOSE) << "Sorting available connections:";
  for (uint32 i = 0; i < connections_.size(); ++i) {
    LOG(LS_VERBOSE) << connections_[i]->ToString();
  }

  Connection* top_connection =
      (connections_.size() > 0) ? connections_[0] : nullptr;

  // If necessary, switch to the new choice.
  // Note that |top_connection| doesn't have to be writable to become the best
  // connection although it will have higher priority if it is writable.
  // The controlled side can switch the best connection only if the current
  // |best connection_| has not been nominated by the controlling side yet.

  // We don't want to pick the best connections if channel is using RFC5245.
  if ((protocol_type_ != ICEPROTO_RFC5245 || ice_role_ == ICEROLE_CONTROLLING ||
      !best_nominated_connection()) &&
      ShouldSwitch(best_connection_, top_connection)) {
    LOG(LS_INFO) << "Switching best connection: " << top_connection->ToString();
    SwitchBestConnectionTo(top_connection);
  }

  // Controlled side can prune only if the best connection has been nominated.
  // because otherwise it may delete the connection that will be selected by
  // the controlling side.
  if (protocol_type_ != ICEPROTO_RFC5245 || ice_role_ == ICEROLE_CONTROLLING ||
      best_nominated_connection()) {
    PruneConnections();
  }

  // Check if all connections are timedout.
  bool all_connections_timedout = true;
  for (uint32 i = 0; i < connections_.size(); ++i) {
    if (connections_[i]->write_state() != Connection::STATE_WRITE_TIMEOUT) {
      all_connections_timedout = false;
      break;
    }
  }

  // Now update the writable state of the channel with the information we have
  // so far.
  if (best_connection_ && best_connection_->writable()) {
    HandleWritable();
  } else if (all_connections_timedout) {
    HandleAllTimedOut();
  } else {
    HandleNotWritable();
  }

  // Update the state of this channel.  This method is called whenever the
  // state of any connection changes, so this is a good place to do this.
  UpdateChannelState();
}

Connection* P2PTransportChannel::best_nominated_connection() const {
  return (best_connection_ && best_connection_->nominated()) ? best_connection_
                                                             : nullptr;
}

void P2PTransportChannel::PruneConnections() {
  // We can prune any connection for which there is a connected, writable
  // connection on the same network with better or equal priority.  We leave
  // those with better priority just in case they become writable later (at
  // which point, we would prune out the current best connection).  We leave
  // connections on other networks because they may not be using the same
  // resources and they may represent very distinct paths over which we can
  // switch. If the |primier| connection is not connected, we may be
  // reconnecting a TCP connection and temporarily do not prune connections in
  // this network. See the big comment in CompareConnections.

  // Get a list of the networks that we are using.
  std::set<rtc::Network*> networks;
  for (const Connection* conn : connections_) {
    networks.insert(conn->port()->Network());
  }
  for (rtc::Network* network : networks) {
    Connection* primier = GetBestConnectionOnNetwork(network);
    if (!(primier && primier->writable() && primier->connected())) {
      continue;
    }

    for (Connection* conn : connections_) {
      if ((conn != primier) && (conn->port()->Network() == network) &&
          (CompareConnectionCandidates(primier, conn) >= 0)) {
        conn->Prune();
      }
    }
  }
}

// Track the best connection, and let listeners know
void P2PTransportChannel::SwitchBestConnectionTo(Connection* conn) {
  // Note: if conn is NULL, the previous best_connection_ has been destroyed,
  // so don't use it.
  Connection* old_best_connection = best_connection_;
  best_connection_ = conn;
  if (best_connection_) {
    if (old_best_connection) {
      LOG_J(LS_INFO, this) << "Previous best connection: "
                           << old_best_connection->ToString();
    }
    LOG_J(LS_INFO, this) << "New best connection: "
                         << best_connection_->ToString();
    SignalRouteChange(this, best_connection_->remote_candidate());
    // When it just switched to a best connection, set receiving to true.
    set_receiving(true);
  } else {
    LOG_J(LS_INFO, this) << "No best connection";
  }
}

void P2PTransportChannel::UpdateChannelState() {
  // The Handle* functions already set the writable state.  We'll just double-
  // check it here.
  bool writable = ((best_connection_ != NULL)  &&
      (best_connection_->write_state() ==
      Connection::STATE_WRITABLE));
  ASSERT(writable == this->writable());
  if (writable != this->writable())
    LOG(LS_ERROR) << "UpdateChannelState: writable state mismatch";

  bool readable = false;
  for (uint32 i = 0; i < connections_.size(); ++i) {
    if (connections_[i]->read_state() == Connection::STATE_READABLE) {
      readable = true;
      break;
    }
  }
  set_readable(readable);
}

// We checked the status of our connections and we had at least one that
// was writable, go into the writable state.
void P2PTransportChannel::HandleWritable() {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  if (!writable()) {
    for (uint32 i = 0; i < allocator_sessions_.size(); ++i) {
      if (allocator_sessions_[i]->IsGettingPorts()) {
        allocator_sessions_[i]->StopGettingPorts();
      }
    }
  }

  was_writable_ = true;
  set_writable(true);
}

// Notify upper layer about channel not writable state, if it was before.
void P2PTransportChannel::HandleNotWritable() {
  ASSERT(worker_thread_ == rtc::Thread::Current());
  if (was_writable_) {
    was_writable_ = false;
    set_writable(false);
  }
}

void P2PTransportChannel::HandleAllTimedOut() {
  // Currently we are treating this as channel not writable.
  HandleNotWritable();
}

// If we have a best connection, return it, otherwise return top one in the
// list (later we will mark it best).
Connection* P2PTransportChannel::GetBestConnectionOnNetwork(
    rtc::Network* network) const {
  // If the best connection is on this network, then it wins.
  if (best_connection_ && (best_connection_->port()->Network() == network))
    return best_connection_;

  // Otherwise, we return the top-most in sorted order.
  for (uint32 i = 0; i < connections_.size(); ++i) {
    if (connections_[i]->port()->Network() == network)
      return connections_[i];
  }

  return NULL;
}

// Handle any queued up requests
void P2PTransportChannel::OnMessage(rtc::Message *pmsg) {
  switch (pmsg->message_id) {
    case MSG_SORT:
      OnSort();
      break;
    case MSG_PING:
      OnPing();
      break;
    case MSG_CHECK_RECEIVING:
      OnCheckReceiving();
      break;
    default:
      ASSERT(false);
      break;
  }
}

// Handle queued up sort request
void P2PTransportChannel::OnSort() {
  // Resort the connections based on the new statistics.
  SortConnections();
}

// Handle queued up ping request
void P2PTransportChannel::OnPing() {
  // Make sure the states of the connections are up-to-date (since this affects
  // which ones are pingable).
  UpdateConnectionStates();

  // Find the oldest pingable connection and have it do a ping.
  Connection* conn = FindNextPingableConnection();
  if (conn)
    PingConnection(conn);

  // Post ourselves a message to perform the next ping.
  uint32 delay = writable() ? WRITABLE_DELAY : UNWRITABLE_DELAY;
  thread()->PostDelayed(delay, this, MSG_PING);
}

void P2PTransportChannel::OnCheckReceiving() {
  // Check receiving only if the best connection has received data packets
  // because we want to detect not receiving any packets only after the media
  // have started flowing.
  if (best_connection_ && best_connection_->recv_total_bytes() > 0) {
    bool receiving = rtc::Time() <=
        best_connection_->last_received() + receiving_timeout_;
    set_receiving(receiving);
  }

  thread()->PostDelayed(check_receiving_delay_, this, MSG_CHECK_RECEIVING);
}

// Is the connection in a state for us to even consider pinging the other side?
// We consider a connection pingable even if it's not connected because that's
// how a TCP connection is kicked into reconnecting on the active side.
bool P2PTransportChannel::IsPingable(Connection* conn) {
  const Candidate& remote = conn->remote_candidate();
  // We should never get this far with an empty remote ufrag.
  ASSERT(!remote.username().empty());
  if (remote.username().empty() || remote.password().empty()) {
    // If we don't have an ICE ufrag and pwd, there's no way we can ping.
    return false;
  }

  // An never connected connection cannot be written to at all, so pinging is
  // out of the question. However, if it has become WRITABLE, it is in the
  // reconnecting state so ping is needed.
  if (!conn->connected() && conn->write_state() != Connection::STATE_WRITABLE) {
    return false;
  }

  if (writable()) {
    // If we are writable, then we only want to ping connections that could be
    // better than this one, i.e., the ones that were not pruned.
    return (conn->write_state() != Connection::STATE_WRITE_TIMEOUT);
  } else {
    // If we are not writable, then we need to try everything that might work.
    // This includes both connections that do not have write timeout as well as
    // ones that do not have read timeout.  A connection could be readable but
    // be in write-timeout if we pruned it before.  Since the other side is
    // still pinging it, it very well might still work.
    return (conn->write_state() != Connection::STATE_WRITE_TIMEOUT) ||
           (conn->read_state() != Connection::STATE_READ_TIMEOUT);
  }
}

// Returns the next pingable connection to ping.  This will be the oldest
// pingable connection unless we have a connected, writable connection that is
// past the maximum acceptable ping delay. When reconnecting a TCP connection,
// the best connection is disconnected, although still WRITABLE while
// reconnecting. The newly created connection should be selected as the ping
// target to become writable instead. See the big comment in CompareConnections.
Connection* P2PTransportChannel::FindNextPingableConnection() {
  uint32 now = rtc::Time();
  if (best_connection_ && best_connection_->connected() &&
      (best_connection_->write_state() == Connection::STATE_WRITABLE) &&
      (best_connection_->last_ping_sent() + MAX_CURRENT_WRITABLE_DELAY <=
       now)) {
    return best_connection_;
  }

  // First, find "triggered checks".  We ping first those connections
  // that have received a ping but have not sent a ping since receiving
  // it (last_received_ping > last_sent_ping).  But we shouldn't do
  // triggered checks if the connection is already writable.
  Connection* oldest_needing_triggered_check = nullptr;
  Connection* oldest = nullptr;
  for (Connection* conn : connections_) {
    if (!IsPingable(conn)) {
      continue;
    }
    bool needs_triggered_check =
        (protocol_type_ == ICEPROTO_RFC5245 &&
         !conn->writable() &&
         conn->last_ping_received() > conn->last_ping_sent());
    if (needs_triggered_check &&
        (!oldest_needing_triggered_check ||
         (conn->last_ping_received() <
          oldest_needing_triggered_check->last_ping_received()))) {
      oldest_needing_triggered_check = conn;
    }
    if (!oldest || (conn->last_ping_sent() < oldest->last_ping_sent())) {
      oldest = conn;
    }
  }

  if (oldest_needing_triggered_check) {
    LOG(LS_INFO) << "Selecting connection for triggered check: " <<
        oldest_needing_triggered_check->ToString();
    return oldest_needing_triggered_check;
  }
  return oldest;
}

// Apart from sending ping from |conn| this method also updates
// |use_candidate_attr| flag. The criteria to update this flag is
// explained below.
// Set USE-CANDIDATE if doing ICE AND this channel is in CONTROLLING AND
//    a) Channel is in FULL ICE AND
//      a.1) |conn| is the best connection OR
//      a.2) there is no best connection OR
//      a.3) the best connection is unwritable OR
//      a.4) |conn| has higher priority than best_connection.
//    b) we're doing LITE ICE AND
//      b.1) |conn| is the best_connection AND
//      b.2) |conn| is writable.
void P2PTransportChannel::PingConnection(Connection* conn) {
  bool use_candidate = false;
  if (protocol_type_ == ICEPROTO_RFC5245) {
    if (remote_ice_mode_ == ICEMODE_FULL && ice_role_ == ICEROLE_CONTROLLING) {
      use_candidate = (conn == best_connection_) ||
                      (best_connection_ == NULL) ||
                      (!best_connection_->writable()) ||
                      (conn->priority() > best_connection_->priority());
    } else if (remote_ice_mode_ == ICEMODE_LITE && conn == best_connection_) {
      use_candidate = best_connection_->writable();
    }
  }
  conn->set_use_candidate_attr(use_candidate);
  conn->Ping(rtc::Time());
}

// When a connection's state changes, we need to figure out who to use as
// the best connection again.  It could have become usable, or become unusable.
void P2PTransportChannel::OnConnectionStateChange(Connection* connection) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  // Update the best connection if the state change is from pending best
  // connection and role is controlled.
  if (protocol_type_ == ICEPROTO_RFC5245 && ice_role_ == ICEROLE_CONTROLLED) {
    if (connection == pending_best_connection_ && connection->writable()) {
      pending_best_connection_ = NULL;
      LOG(LS_INFO) << "Switching best connection on controlled side"
                   << " because it's now writable: " << connection->ToString();
      SwitchBestConnectionTo(connection);
    }
  }

  // We have to unroll the stack before doing this because we may be changing
  // the state of connections while sorting.
  RequestSort();
}

// When a connection is removed, edit it out, and then update our best
// connection.
void P2PTransportChannel::OnConnectionDestroyed(Connection* connection) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  // Note: the previous best_connection_ may be destroyed by now, so don't
  // use it.

  // Remove this connection from the list.
  std::vector<Connection*>::iterator iter =
      std::find(connections_.begin(), connections_.end(), connection);
  ASSERT(iter != connections_.end());
  connections_.erase(iter);

  LOG_J(LS_INFO, this) << "Removed connection ("
    << static_cast<int>(connections_.size()) << " remaining)";

  if (pending_best_connection_ == connection) {
    pending_best_connection_ = NULL;
  }

  // If this is currently the best connection, then we need to pick a new one.
  // The call to SortConnections will pick a new one.  It looks at the current
  // best connection in order to avoid switching between fairly similar ones.
  // Since this connection is no longer an option, we can just set best to NULL
  // and re-choose a best assuming that there was no best connection.
  if (best_connection_ == connection) {
    LOG(LS_INFO) << "Best connection destroyed.  Will choose a new one.";
    SwitchBestConnectionTo(NULL);
    RequestSort();
  }

  SignalConnectionRemoved(this);
}

// When a port is destroyed remove it from our list of ports to use for
// connection attempts.
void P2PTransportChannel::OnPortDestroyed(PortInterface* port) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  // Remove this port from the list (if we didn't drop it already).
  std::vector<PortInterface*>::iterator iter =
      std::find(ports_.begin(), ports_.end(), port);
  if (iter != ports_.end())
    ports_.erase(iter);

  LOG(INFO) << "Removed port from p2p socket: "
            << static_cast<int>(ports_.size()) << " remaining";
}

// We data is available, let listeners know
void P2PTransportChannel::OnReadPacket(
    Connection *connection, const char *data, size_t len,
    const rtc::PacketTime& packet_time) {
  ASSERT(worker_thread_ == rtc::Thread::Current());

  // Do not deliver, if packet doesn't belong to the correct transport channel.
  if (!FindConnection(connection))
    return;

  // Let the client know of an incoming packet
  SignalReadPacket(this, data, len, packet_time, 0);

  // May need to switch the sending connection based on the receiving media path
  // if this is the controlled side.
  if (ice_role_ == ICEROLE_CONTROLLED && !best_nominated_connection() &&
      connection->writable() && best_connection_ != connection) {
    SwitchBestConnectionTo(connection);
  }
}

void P2PTransportChannel::OnReadyToSend(Connection* connection) {
  if (connection == best_connection_ && writable()) {
    SignalReadyToSend(this);
  }
}

}  // namespace cricket
