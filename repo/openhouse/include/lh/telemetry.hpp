// The driver's live telemetry: what the viewer (openhouse-viewer) shows.
//
// The driver listens on 127.0.0.1 (never other hosts) and streams newline-delimited
// JSON to every client. On connect a client gets {"type":"hello"} and the recent log
// backlog; then {"type":"state"} about 20 times a second and {"type":"log"} for every
// message, at every level (the client decides what to show). The protocol only adds
// fields; clients ignore what they don't know.
//
//   hello  {"type":"hello","protocol":1,"name":"openhouse","host":"...","pid":N}
//   log    {"type":"log","t":S,"level":"DEBUG|INFO|WARN|ERR","serial":"...","text":"..."}
//   state  {"type":"state","t":S,
//           "room":{"id":"...","settled":B,"epoch":N,"settleSigma":M,"resets":N,
//                   "learned":N,"seen":N},
//           "bases":[{"serial","channel","mode","seen","placed","origin","locked",
//                     "observations","sigma","learned","status","p":[x,y,z],"q":[w,x,y,z],"faults"}],
//           "devices":[{"serial","role","model","connected","enabled","battery","charging",
//                       "state","result","valid","p","q","v","primary","bases":[...],
//                       "coverage":[{"base","frames","sensors"}],"basesSeen":N}],
//           "receivers":[{"serial","connected"}]}
//
// Positions are metres in the driver's room frame; sigma metres (1-sigma, total). A base's
// "status" is "not seen", "not placed", "learning" or "learned" ("learned": good enough
// to track and align against; the room settles once every seen base is). "resets" bumps
// when the room is reset (SteamVR settings, or its file deleted while running).
#pragma once

namespace lh::telemetry {

constexpr int kProtocolVersion = 1;
constexpr int kDefaultPort = 36840;
// LH_TELEMETRY_PORT, else kDefaultPort; 0 turns the server off.
int PortFromEnvironment();

}  // namespace lh::telemetry
