import { AnimatePresence, motion, MotionProps } from "framer-motion";
import React from "react";

import { cx } from "@/cva.config";

export type AnimationDirection = "up" | "down" | "left" | "right";
interface SlideAnimationProps {
  direction: AnimationDirection;
  children: React.ReactNode;
  isVisible: boolean;
  className?: string;
  onAnimationComplete?: () => void;
}

const SlideAnimation: React.FC<SlideAnimationProps> = ({
                                                         direction,
                                                         children,
                                                         isVisible,
                                                         className = "",
                                                         onAnimationComplete
                                                       }) => {
  const getAnimationVariants = (): MotionProps["variants"] => {
    const distance = 50;
    const variants = {
      up: {
        initial: { y: -distance, opacity: 0 },
        animate: { y: 0, opacity: 1 },
        exit: { y: -distance, opacity: 0 }
      },
      down: {
        initial: { y: distance, opacity: 0 },
        animate: { y: 0, opacity: 1 },
        exit: { y: distance, opacity: 0 }
      },
      left: {
        initial: { x: -distance, opacity: 0 },
        animate: { x: 0, opacity: 1 },
        exit: { x: -distance, opacity: 0 }
      },
      right: {
        initial: { x: distance, opacity: 0 },
        animate: { x: 0, opacity: 1 },
        exit: { x: distance, opacity: 0 }
      }
    };

    return {
      initial: variants[direction].initial,
      animate: {
        ...variants[direction].animate,
        transition: {
          duration: 0.3,
          ease: "easeOut"
        }
      },
      exit: {
        ...variants[direction].exit,
        transition: {
          duration: 0.2,
          ease: "easeIn"
        }
      }
    };
  };

  return (
    <AnimatePresence>
      {isVisible && (
        <motion.div
          key={`slide-${direction}`}
          initial="initial"
          animate="animate"
          exit="exit"
          variants={getAnimationVariants()}
          className={cx(className)}
          onAnimationComplete={onAnimationComplete}
          style={{
            willChange: "transform, opacity"
          }}
          transition={{
            duration: 0.5,
            ease: "easeInOut",
            x: { duration: 0.5, ease: "easeInOut" },
            y: { duration: 0.5, ease: "easeInOut" },
            opacity: { duration: 0.4, ease: "easeInOut" }
          }}
        >
          {children}
        </motion.div>
      )}
    </AnimatePresence>
  );
}
export default SlideAnimation;