import { useState, useEffect } from "react";
import { Button as AntdButton, Select, Checkbox } from "antd";
import { useReactAt } from "i18n-auto-extractor/react";
import { isMobile } from "react-device-detect";

import { TextAreaWithLabel } from "@components/TextArea";
import { useJsonRpc } from "@/hooks/useJsonRpc";
import { useSettingsStore } from "@/hooks/stores";
import { SettingsItem } from "@components/Settings/SettingsView";
import { SettingsPageHeader } from "@components/Settings/SettingsPageheader";
import { SettingsPageLayout } from "@/components/Settings/SettingsPageLayout";
import notifications from "../../../notifications";

const defaultEdid =
  "00ffffffffffff0052620188008888881c150103800000780a0dc9a05747982712484c00000001010101010101010101010101010101023a801871382d40582c4500c48e2100001e011d007251d01e206e285500c48e2100001e000000fc00543734392d6648443732300a20000000fd00147801ff1d000a202020202020017b";
const edids = [
  {
    value: defaultEdid.toUpperCase(),
    label: "KVM Default",
  },
  {
    value:
      "00FFFFFFFFFFFF00047265058A3F6101101E0104A53420783FC125A8554EA0260D5054BFEF80714F8140818081C081008B009500B300283C80A070B023403020360006442100001A000000FD00304C575716010A202020202020000000FC0042323436574C0A202020202020000000FF0054384E4545303033383532320A01F802031CF14F900504030201111213141F2309070783010000011D8018711C1620582C250006442100009E011D007251D01E206E28550006442100001E8C0AD08A20E02D10103E9600064421000018C344806e70B028401720A80406442100001E00000000000000000000000000000000000000000000000000000096",
    label: "Acer B246WL, 1920x1200",
  },
  {
    value:
      "00FFFFFFFFFFFF0006B3872401010101021F010380342078EA6DB5A7564EA0250D5054BF6F00714F8180814081C0A9409500B300D1C0283C80A070B023403020360006442100001A000000FD00314B1E5F19000A202020202020000000FC00504132343851560A2020202020000000FF004D314C4D51533035323135370A014D02032AF14B900504030201111213141F230907078301000065030C001000681A00000101314BE6E2006A023A801871382D40582C450006442100001ECD5F80B072B0374088D0360006442100001C011D007251D01E206E28550006442100001E8C0AD08A20E02D10103E960006442100001800000000000000000000000000DC",
    label: "ASUS PA248QV, 1920x1200",
  },
  {
    value:
      "00FFFFFFFFFFFF0010AC132045393639201E0103803C22782ACD25A3574B9F270D5054A54B00714F8180A9C0D1C00101010101010101023A801871382D40582C450056502100001E000000FF00335335475132330A2020202020000000FC0044454C4C204432373231480A20000000FD00384C1E5311000A202020202020018102031AB14F90050403020716010611121513141F65030C001000023A801871382D40582C450056502100001E011D8018711C1620582C250056502100009E011D007251D01E206E28550056502100001E8C0AD08A20E02D10103E960056502100001800000000000000000000000000000000000000000000000000000000004F",
    label: "DELL D2721H, 1920x1080",
  },
];

const streamQualityOptions = [
  { value: "1", label: "High" },
  { value: "0.5", label: "Medium" },
  { value: "0.1", label: "Low" },
];

export default function VideoContent() {
  const { $at } = useReactAt();
  const [send] = useJsonRpc();
  const [streamQuality, setStreamQuality] = useState("1");
  const [streamEncodecType, setStreamEncodecType] = useState("avc");
  const [customEdidValue, setCustomEdidValue] = useState<string | null>(null);
  const [edid, setEdid] = useState<string | null>(null);
  const [forceHpd, setForceHpd] = useState(false);

  // Video enhancement settings from store
  const videoSaturation = useSettingsStore(state => state.videoSaturation);
  const setVideoSaturation = useSettingsStore(state => state.setVideoSaturation);
  const videoBrightness = useSettingsStore(state => state.videoBrightness);
  const setVideoBrightness = useSettingsStore(state => state.setVideoBrightness);
  const videoContrast = useSettingsStore(state => state.videoContrast);
  const setVideoContrast = useSettingsStore(state => state.setVideoContrast);

  useEffect(() => {
    send("getStreamEncodecType", {}, resp => {
      if ("error" in resp) return;
      setStreamEncodecType(resp.result as string);
    });

    send("getStreamQualityFactor", {}, resp => {
      if ("error" in resp) return;
      setStreamQuality(String(resp.result));
    });

    send("getEDID", {}, resp => {
      if ("error" in resp) {
        notifications.error(`Failed to get EDID: ${resp.error.data || "Unknown error"}`);
        return;
      }

      const receivedEdid = resp.result as string;

      const matchingEdid = edids.find(
        x => x.value.toLowerCase() === receivedEdid.toLowerCase(),
      );

      if (matchingEdid) {
        setEdid(matchingEdid.value.toUpperCase());
        setCustomEdidValue(null);
      } else {
        setEdid("custom");
        setCustomEdidValue(receivedEdid);
      }
    });

    send("getForceHpd", {}, resp => {
      if ("error" in resp) {
        notifications.error(`Failed to get force EDID output: ${resp.error.data || "Unknown error"}`);
        setForceHpd(false);
        return;
      }

      setForceHpd(resp.result as boolean);
    });
  }, [send]);

  const handleForceHpdChange = (checked: boolean) => {
    send("setForceHpd", { forceHpd: checked }, resp => {
      if ("error" in resp) {
        notifications.error(`Failed to set force EDID output: ${resp.error.data || "Unknown error"}`);
        setForceHpd(!checked);
        return;
      }

      notifications.success(`Force EDID output ${checked ? "enabled" : "disabled"}`);
      setForceHpd(checked);
    });
  };

  const handleStreamEncodecTypeChange = (encodecType: string) => {
    send("setStreamEncodecType", { encodecType }, resp => {
      if ("error" in resp) {
        notifications.error(
          `Failed to set stream encodec type: ${resp.error.data || "Unknown error"}`,
        );
        return;
      }

      notifications.success(`Stream encodec type set to ${encodecType}`);
      setStreamEncodecType(encodecType);
      window.location.reload();
    });
  };

  const handleStreamQualityChange = (factor: string) => {
    send("setStreamQualityFactor", { factor: Number(factor) }, resp => {
      if ("error" in resp) {
        notifications.error(
          `Failed to set stream quality: ${resp.error.data || "Unknown error"}`,
        );
        return;
      }

      notifications.success(`Stream quality set to ${streamQualityOptions.find(x => x.value === factor)?.label}`);
      setStreamQuality(factor);
    });
  };

  const handleEDIDChange = (newEdid: string) => {
    send("setEDID", { edid: newEdid }, resp => {
      if ("error" in resp) {
        notifications.error(`Failed to set EDID: ${resp.error.data || "Unknown error"}`);
        return;
      }

      notifications.success(
        `EDID set successfully to ${edids.find(x => x.value === newEdid)?.label}`,
      );
      setEdid(newEdid);
    });
  };

  return (
    <SettingsPageLayout>
      <SettingsPageHeader
        title={$at("Video")}
        description={$at("Configure video quality and display settings")}
      />

      <div className="space-y-4">
        <SettingsItem
          title={$at("Encodec Type")}
          description={""}
        >
          <Select
            className={isMobile ? "w-full bg-transparent" : ""}
            value={streamEncodecType}
            options={[
              { value: "avc", label: "H.264 (AVC)" },
              { value: "hevc", label: "H.265 (HEVC)" },
            ]}
            onChange={e => handleStreamEncodecTypeChange(e)}
          />
        </SettingsItem>

        <SettingsItem
          title={$at("Stream Quality")}
          description={""}
        >
          <Select
            className={isMobile ? "w-full bg-transparent" : ""}
            value={streamQuality}
            options={streamQualityOptions}
            onChange={e => handleStreamQualityChange(e)}
          />
        </SettingsItem>

        {/* Video Enhancement Settings */}
        <SettingsItem
          title={$at("Video Enhancement")}
          description={$at("Adjust color settings to make the video output more vibrant and colorful")}
        />

        <div className="space-y-4 pl-4 border-l-2 border-slate-200 dark:border-slate-700 ml-2">
          {/* Saturation */}
          <SettingsItem
            title={$at("Saturation")}
            description={`${videoSaturation.toFixed(1)}x`}
          >
            <input
              type="range"
              min="0.5"
              max="2.0"
              step="0.1"
              value={videoSaturation}
              onChange={e => setVideoSaturation(Number.parseFloat(e.target.value))}
              className="h-2 w-32 cursor-pointer appearance-none rounded-lg bg-gray-200 dark:bg-gray-700"
            />
          </SettingsItem>

          {/* Brightness */}
          <SettingsItem
            title={$at("Brightness")}
            description={`${videoBrightness.toFixed(1)}x`}
          >
            <input
              type="range"
              min="0.5"
              max="2.0"
              step="0.1"
              value={videoBrightness}
              onChange={e => setVideoBrightness(Number.parseFloat(e.target.value))}
              className="h-2 w-32 cursor-pointer appearance-none rounded-lg bg-gray-200 dark:bg-gray-700"
            />
          </SettingsItem>

          {/* Contrast */}
          <SettingsItem
            title={$at("Contrast")}
            description={`${videoContrast.toFixed(1)}x`}
          >
            <input
              type="range"
              min="0.5"
              max="2.0"
              step="0.1"
              value={videoContrast}
              onChange={e => setVideoContrast(Number.parseFloat(e.target.value))}
              className="h-2 w-32 cursor-pointer appearance-none rounded-lg bg-gray-200 dark:bg-gray-700"
            />
          </SettingsItem>

          <div className="flex gap-2">
            <AntdButton
              type="primary"
              onClick={() => {
                setVideoSaturation(1.0);
                setVideoBrightness(1.0);
                setVideoContrast(1.0);
              }}
            >{$at("Reset to Default")}</AntdButton>
          </div>
        </div>

        {/* EDID Force Output Setting */}
        <SettingsItem
          title={$at("Force EDID Output")}
          description={$at("Force EDID output even when no display is connected")}
        >
          <Checkbox
            checked={forceHpd}
            onChange={e => handleForceHpdChange(e.target.checked)}
          />
        </SettingsItem>

        <SettingsItem
          title="EDID"
          description={$at("Adjust the EDID settings for the display")}
        >
          <Select
            className={isMobile ? "w-full bg-transparent" : ""}
            value={customEdidValue ? "custom" : edid || ""}
            optionLabelProp={"label"}
            onChange={e => {
              if (e === "custom") {
                setEdid("custom");
                setCustomEdidValue("");
              } else {
                setCustomEdidValue(null);
                handleEDIDChange(e);
              }
            }}
            options={[...edids, { value: "custom", label: "Custom" }]}
          />
        </SettingsItem>

        {customEdidValue !== null && (
          <div className="space-y-4">
            <SettingsItem
              title={$at("Custom EDID")}
              description={$at("EDID details video mode compatibility. Default settings works in most cases, but unique UEFI/BIOS might need adjustments.")}
            />
            <TextAreaWithLabel
              label={$at("EDID File")}
              placeholder="00F..."
              rows={3}
              value={customEdidValue}
              onChange={e => setCustomEdidValue(e.target.value)}
            />
            <div className="flex justify-start gap-x-2">
              <AntdButton
                type="primary"
                onClick={() => handleEDIDChange(customEdidValue)}
              >{$at("Set Custom EDID")}</AntdButton>
              <AntdButton
                className={"border-2"}
                style={{
                  background: "transparent",
                  borderColor: "rgba(28,168,0,1)",
                  whiteSpace: "nowrap",
                }}
                onClick={() => {
                  setCustomEdidValue(null);
                  handleEDIDChange(defaultEdid.toUpperCase());
                }}
              >
                <span className={"text-[rgba(28,168,0,1)]"}>{$at("Restore to default")}</span>
              </AntdButton>
            </div>
          </div>
        )}
      </div>
    </SettingsPageLayout>
  );
}