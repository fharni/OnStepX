//--------------------------------------------------------------------------------------------------
// telescope mount control, guiding
#include <Arduino.h>
#include "../../Constants.h"
#include "../../Config.h"
#include "../../ConfigX.h"
#include "../HAL/HAL.h"
#include "../pinmaps/Models.h"
#include "../debug/Debug.h"

#if AXIS1_DRIVER_MODEL != OFF && AXIS2_DRIVER_MODEL != OFF

#include "../coordinates/Transform.h"
#include "../commands/ProcessCmds.h"
#include "../motion/Axis.h"
#include "Mount.h"

double Mount::guideRateSelectToRate(GuideRateSelect guideRateSelect, uint8_t axis) {
  switch (guideRateSelect) {
    case GR_QUARTER: return 0.25;
    case GR_HALF: return 0.5;
    case GR_1X: return 1.0;
    case GR_2X: return 2.0;
    case GR_4X: return 4.0;
    case GR_8X: return 8.0;
    case GR_20X: return 20.0;
    case GR_48X: return 48.0;
    case GR_HALF_MAX: return (2000000.0/misc.usPerStepCurrent)/degToRad(axis1.getStepsPerMeasure());
    case GR_MAX: return (1000000.0/misc.usPerStepCurrent)/degToRad(axis1.getStepsPerMeasure());
    case GR_CUSTOM: if (axis == 1) return 48.0; else if (axis == 2) return 48.0; else return 0;
    default: return 0;
  }
}

bool Mount::validGuideAxis1(GuideAction guideAction) {
  if (!limitsEnabled) return true;
  updatePosition();
  if (guideAction == GA_FORWARD) {
    if (meridianFlip != MF_NEVER && current.pierSide == PIER_SIDE_EAST) { if (current.h < -limits.pastMeridianE) return false; }
    if (current.h < axis1.settings.limits.min) return false;
  }
  if (guideAction == GA_REVERSE) {
    if (meridianFlip != MF_NEVER && current.pierSide == PIER_SIDE_WEST) { if (current.h > limits.pastMeridianW) return false; }
    if (current.h > axis1.settings.limits.max) return false;
  }
  return true;
}

bool Mount::validGuideAxis2(GuideAction guideAction) {
  double a2;
  if (!limitsEnabled) return true;
  updatePosition();
  transform.equToHor(&current);
  #if AXIS2_TANGENT_ARM == ON
    a2 = axis2.getInstrumentCoordinate();
  #else
    if (transform.mountType == ALTAZM) a2 = current.a; else a2 = current.d;
  #endif
  if (guideAction == GA_FORWARD) {
    if (a2 < axis2.settings.limits.min && current.pierSide == PIER_SIDE_WEST) return false;
    if (a2 > axis2.settings.limits.max && current.pierSide == PIER_SIDE_EAST) return false;
    if (transform.mountType == ALTAZM && current.a > limits.altitude.max) return false;
  }
  if (guideAction == GA_REVERSE) {
    if (a2 < axis2.settings.limits.min && current.pierSide == PIER_SIDE_EAST) return false;
    if (a2 > axis2.settings.limits.max && current.pierSide == PIER_SIDE_WEST) return false;
    if (transform.mountType == ALTAZM && current.a < limits.altitude.min) return false;
  }
  return true;
}

CommandError Mount::startGuideAxis1(GuideAction guideAction, GuideRateSelect guideRateSelect, unsigned long guideTimeLimit) {
  if (guideAction == GA_NONE || guideActionAxis1 == guideAction) return CE_NONE;
  if (axis1.error.driverFault || axis1.error.motorFault) return CE_SLEW_ERR_HARDWARE_FAULT;
  if (parkState == PS_PARKED)          return CE_SLEW_ERR_IN_PARK;
  if (gotoState != GS_NONE)            return CE_SLEW_IN_MOTION;
  if (isSpiralGuiding())               return CE_SLEW_IN_MOTION;
  if (!validGuideAxis1(guideAction))   return CE_SLEW_ERR_OUTSIDE_LIMITS;
  if (guideRateSelect < 3) {
    if (anyError())                    return CE_SLEW_ERR_OUTSIDE_LIMITS;
    if (axis1.motionError())           return CE_SLEW_ERR_OUTSIDE_LIMITS;
  }

  guideActionAxis1 = guideAction;
 
  double rate = guideRateSelectToRate(guideRateSelect);

  VF("MSG: startGuideAxis1(); guide ");
  if (guideAction == GA_REVERSE) VF("reverse"); else VF("forward"); VF(" started at "); V(rate); VL("X");

  if (rate <= 2) {
    if (guideAction == GA_REVERSE) guideRateAxis1 = -rate; else guideRateAxis1 = rate;
    updateTrackingRates();
  } else {
    axis1.setFrequencyMax(degToRad(rate/240.0));
    if (guideAction == GA_REVERSE) axis1.autoSlew(DIR_REVERSE); else axis1.autoSlew(DIR_FORWARD);
  }

  // unlimited 0 means the maximum period, about 49 days
  if (guideTimeLimit == 0) guideTimeLimit = 0xFFFFFFFF;
  guideFinishTimeAxis1 = millis() + guideTimeLimit;

  return CE_NONE;
}

CommandError Mount::startGuideAxis2(GuideAction guideAction, GuideRateSelect guideRateSelect, unsigned long guideTimeLimit) {
  if (guideAction == GA_NONE || guideActionAxis2 == guideAction) return CE_NONE;
  if (axis2.error.driverFault || axis2.error.motorFault) return CE_SLEW_ERR_HARDWARE_FAULT;
  if (parkState == PS_PARKED)          return CE_SLEW_ERR_IN_PARK;
  if (gotoState != GS_NONE)            return CE_SLEW_IN_MOTION;
  if (isSpiralGuiding())               return CE_SLEW_IN_MOTION;
  if (!validGuideAxis2(guideAction))   return CE_SLEW_ERR_OUTSIDE_LIMITS;
  if (guideRateSelect < 3) {
    if (anyError())                    return CE_SLEW_ERR_OUTSIDE_LIMITS;
    if (axis2.motionError())           return CE_SLEW_ERR_OUTSIDE_LIMITS;
  }

  guideActionAxis2 = guideAction;

  double rate = guideRateSelectToRate(guideRateSelect);

  VF("MSG: startGuideAxis2(); guide ");
  if (guideAction == GA_REVERSE) VF("reverse"); else VF("forward"); VF(" started at "); V(rate); VL("X");

  if (rate <= 2) {
    if (guideAction == GA_REVERSE) guideRateAxis2 = -rate; else guideRateAxis2 = rate;
    updateTrackingRates();
  } else {
    axis2.setFrequencyMax(degToRad(rate/240.0));
    if (guideAction == GA_REVERSE) axis2.autoSlew(DIR_REVERSE); else axis2.autoSlew(DIR_FORWARD);
  }
  
  // unlimited 0 means the maximum period, about 49 days
  if (guideTimeLimit == 0) guideTimeLimit = 0xFFFFFFFF;
  guideFinishTimeAxis2 = millis() + guideTimeLimit;

  return CE_NONE;
}

void Mount::stopGuideAxis1() {
  if (guideActionAxis1 > GA_BREAK) {
    if (guideRateAxis1 == 0.0) {
      VLF("MSG: stopGuideAxis1(); requesting guide stop");
      guideActionAxis1 = GA_BREAK;
      axis1.autoSlewStop();
    } else {
      VLF("MSG: stopGuideAxis1(); guide stopped");
      guideActionAxis1 = GA_NONE;
      guideRateAxis1 = 0.0;
      updateTrackingRates();
    }
  }
}

void Mount::stopGuideAxis2() {
  if (guideActionAxis2 > GA_BREAK) {
    if (guideRateAxis2 == 0.0) {
      VLF("MSG: stopGuideAxis2(); requesting guide stop");
      guideActionAxis2 = GA_BREAK;
      axis2.autoSlewStop();
    } else {
      VLF("MSG: stopGuideAxis2(); guide stopped");
      guideActionAxis2 = GA_NONE;
      guideRateAxis2 = 0.0;
      updateTrackingRates();
    }
  }
}

void Mount::pollGuides() {
  // check fast guide completion axis1
  if (guideActionAxis1 == GA_BREAK && guideRateAxis1 == 0.0 && !axis1.autoSlewActive()) {
    guideActionAxis1 = GA_NONE;
    updateTrackingRates();
  } else {
    // check for guide timeout axis1
    if (guideActionAxis1 > GA_BREAK && (long)(millis() - guideFinishTimeAxis1) >= 0) stopGuideAxis1();
  }

  // check fast guide completion axis2
  if (guideActionAxis2 == GA_BREAK && guideRateAxis2 == 0.0 && !axis2.autoSlewActive()) {
    guideActionAxis2 = GA_NONE;
    updateTrackingRates();
  } else {
    // check for guide timeout axis2
    if (guideActionAxis2 > GA_BREAK && (long)(millis() - guideFinishTimeAxis2) >= 0) stopGuideAxis2();
  }
}

bool Mount::isSpiralGuiding() {
  return false;  
}

#endif
