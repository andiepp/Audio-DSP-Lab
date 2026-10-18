// ****************************************************************************
// ***                           W I R E L E S S                            ***
// ****************************************************************************
//  The panel as the board's wireless port: it joins the WiFi the board
//  keeps for its radio (network 0 first, then 1; the link reads them with
//  "wifi secrets"), announces itself as audio-dsp-lab.local, and takes
//  one PC on TCP port 7000. That PC speaks the board's protocol through
//  the panel; the link shares the cable between it and the panel's own
//  screens (Link.cpp).
// ----------------------------------------------------------------------------

#ifndef PANEL_WIRELESS_H
#define PANEL_WIRELESS_H

#include <Arduino.h>

#define NETWORK_PORT                  7000
#define NETWORK_HOST_NAME             "audio-dsp-lab"

void networkBegin();
void networkPoll();

// the networks to join, from the board; an empty SSID is an empty slot
void networkConfigure(const String ssids[2], const String passwords[2]);

// what the settings page and the board are told
const char *networkState();             // "none", "connecting", "online", "failed"
String networkAddress();                // "192.168.0.205" or "-"
bool networkClientConnected();
uint32_t networkVersion();              // changes when any of the above changes

// the PC on TCP: lines to it (the link calls this)
void networkSend(const char *line);

#endif
