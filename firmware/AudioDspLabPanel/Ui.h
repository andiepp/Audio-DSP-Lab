// ****************************************************************************
// ***                             S C R E E N S                            ***
// ****************************************************************************
//  Phase 1: the home screen (applications, pot control), each
//  application's parameters as a list built from the board's "params",
//  a value editor, the output switch, and the pot strip in the knob
//  colours. The screens read the link's model and refresh when it
//  changes.
// ----------------------------------------------------------------------------

#ifndef PANEL_UI_H
#define PANEL_UI_H

void uiBegin();
void uiPoll();

#include <Arduino.h>
void uiCommand(const String &line);   // a command from the USB port (screenshots, tests)

#endif
