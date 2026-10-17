// ****************************************************************************
// ***                     S E R I A L   P R O T O C O L                    ***
// ****************************************************************************
//  Text lines on the USB serial port (921600 8N1), specified in
//  docs/Protocol.md:
//
//    request    <command> [arguments]                   host -> board
//    response   zero or more "* ..." data lines, then
//               "ok [values]" or "err <code> <message>" board -> host
//    event      "! <event> [values]"                     board -> host
//
//  The board answers requests in the order they came, one response each.
//  Events are written only between responses, and only while "events on"
//  is set, except "! ready", which every start sends.
//
//  Only the loop calls these; nothing else writes to the port.
// ----------------------------------------------------------------------------

#ifndef PROTOCOL_H
#define PROTOCOL_H

void protocolInit();      // opens the port (before the applications start)
void protocolReady();     // announces the start ("! ready ...")
void protocolPoll();      // requests in, responses and events out

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
