import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import {
  FluentProvider,
  createLightTheme,
  webLightTheme
} from "@fluentui/react-components";
import App from "./App";
import "./styles.css";

const factoryTheme = createLightTheme({
  10: "#06132f",
  20: "#102659",
  30: "#19377e",
  40: "#2148a3",
  50: "#2b59c5",
  60: "#3f6ce0",
  70: "#6387e8",
  80: "#86a1ee",
  90: "#a9bbf3",
  100: "#cbd6f8",
  110: "#e5ebfb",
  120: "#f3f6fd",
  130: "#f8faff",
  140: "#fbfcff",
  150: "#fdfdff",
  160: "#ffffff"
});

Object.assign(factoryTheme, {
  ...webLightTheme,
  colorBrandForeground1: "#0875e1",
  colorBrandBackground: "#0875e1",
  colorBrandBackgroundHover: "#0067c7",
  colorBrandBackgroundPressed: "#005bb2",
  borderRadiusMedium: "10px",
  borderRadiusLarge: "16px"
});

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <FluentProvider theme={factoryTheme} className="app-provider">
      <App />
    </FluentProvider>
  </StrictMode>
);
