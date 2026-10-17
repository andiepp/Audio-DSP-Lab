// ****************************************************************************
// ***                   P A R A M E T E R   N A M E S                      ***
// ****************************************************************************
//  What the serial protocol calls each application's parameters and how it
//  writes their values. Applications.cpp holds the parameters themselves
//  (range, default, what a change does) as scaled integers; this table
//  gives each a name, a kind and the decimals of its scale:
//
//    number   decimal text; decimals 2 = the value is kept in 0.01 units
//    integer  whole numbers
//    switch   off / on (0 / 1)
//    choice   one of the names in 'choices', comma separated (index 0..n-1)
//
//  Names are local to an application, as the ids are.
// ----------------------------------------------------------------------------

#ifndef PARAM_INFO_H
#define PARAM_INFO_H

#include <stdint.h>

enum ParamKind : uint8_t {
  KIND_NUMBER  = 0,
  KIND_INTEGER = 1,
  KIND_SWITCH  = 2,
  KIND_CHOICE  = 3,
};

struct ParamInfo {
  uint8_t id;
  const char *name;
  ParamKind kind;
  uint8_t decimals;         // KIND_NUMBER only
  const char *unit;         // "" when the value has none
  const char *choices;      // KIND_CHOICE only
};

// the application's token in the protocol ("generator", ...), nullptr if unknown
const char *appToken(uint8_t appId);
// the application id of a token, 0 when unknown
uint8_t appFromToken(const char *token);

// the parameter's description, nullptr when the table has none
const ParamInfo *paramInfo(uint8_t appId, uint8_t paramId);
const ParamInfo *paramInfoByName(uint8_t appId, const char *name);

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
