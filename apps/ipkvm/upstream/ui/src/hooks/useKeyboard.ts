import { useCallback } from "react";

import { useHidStore } from "@/hooks/stores";
import { useHidRpc } from "@/hooks/useHidRpc";
import { keys, modifiers } from "@/keyboardMappings";

export default function useKeyboard() {
  const { reportKeyboardEvent, reportKeyboardMacroEvent, rpcHidReady } = useHidRpc();

  const updateActiveKeysAndModifiers = useHidStore(
    state => state.updateActiveKeysAndModifiers,
  );
  const isReinitializingGadget = useHidStore(state => state.isReinitializingGadget);
  const usbState = useHidStore(state => state.usbState);

  const sendKeyboardEvent = useCallback(
    (keys: number[], modifiers: number[]) => {
      if (!rpcHidReady) return;
      // Don't send keyboard events while reinitializing gadget
      if (isReinitializingGadget) return;
      if (usbState !== "configured") return;
      const accModifier = modifiers.reduce((acc, val) => acc + val, 0);

      reportKeyboardEvent(keys, accModifier);

      // We do this for the info bar to display the currently pressed keys for the user
      updateActiveKeysAndModifiers({ keys: keys, modifiers: modifiers });
    },
    [rpcHidReady, reportKeyboardEvent, updateActiveKeysAndModifiers, isReinitializingGadget, usbState],
  );

  const resetKeyboardState = useCallback(() => {
    sendKeyboardEvent([], []);
  }, [sendKeyboardEvent]);

  const executeMacro = async (steps: { keys: string[] | null; modifiers: string[] | null; delay: number }[]) => {
    if (!rpcHidReady) return;
    if (isReinitializingGadget) return;
    if (usbState !== "configured") return;

    const macroSteps = steps.map(step => {
      const keyValues = step.keys?.map(key => keys[key]).filter(Boolean) || [];
      const modifierValues = step.modifiers?.map(mod => modifiers[mod]).filter(Boolean) || [];
      const accModifier = modifierValues.reduce((acc, val) => acc + val, 0);

      return {
        modifier: accModifier,
        keys: keyValues,
        delay: step.delay || 50,
      };
    });

    reportKeyboardMacroEvent(false, macroSteps);
  };

  return { sendKeyboardEvent, resetKeyboardState, executeMacro };
}
