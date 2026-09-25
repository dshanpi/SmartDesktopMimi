import type { ActionFunctionArgs } from "react-router";
import { Form, redirect, useActionData } from "react-router-dom";
import { useState } from "react";
import { LuEye, LuEyeOff } from "react-icons/lu";

import SimpleNavbar from "@components/SimpleNavbar";
import Container from "@components/Container";
import Fieldset from "@components/Fieldset";
import { InputFieldWithLabel } from "@components/InputField";
import { Button } from "@components/Button";
import Logo100Ask from "@/assets/logo-100ask.png";
import { DEVICE_API } from "@/ui.config";
import { DeviceStatus } from "@routes/login_page/index";
import { useLanguageSettings } from "@routes/login_page/useLocalAuth";

import api from "../api";
import ExtLink from "../components/ExtLink";


const loader = async () => {
  const res = await api
    .GET(`${DEVICE_API}/device/status`)
    .then(res => res.json() as Promise<DeviceStatus>);

  if (!res.isSetup) return redirect("/mode");

  const deviceRes = await api.GET(`${DEVICE_API}/device`);
  if (deviceRes.ok) return redirect("/");
  return null;
};

const action = async ({ request }: ActionFunctionArgs) => {
  const formData = await request.formData();
  const password = formData.get("password");

  try {
    const response = await api.POST(`${DEVICE_API}/auth/login-local`, {
      password,
    });

    if (response.ok) {
      return redirect("/");
    } else {
      const data = await response.json();
      return { error: data.error || "Invalid password" };
    }
  } catch (error) {
    console.error(error);
    return { error: "An error occurred while logging in" };
  }
};

export default function LoginLocalRoute() {
  const actionData = useActionData() as { error?: string; success?: boolean };
  const [showPassword, setShowPassword] = useState(false);
  const { $at } = useLanguageSettings();

  return (
    <>
      <div className="grid min-h-screen grid-rows-(--grid-layout)">
        <SimpleNavbar />
        <Container>
          <div className="flex h-full w-full items-center justify-center">
            <div className="-mt-32 max-w-2xl space-y-8">
              <div className="flex items-center justify-center">
                <img
                  src={Logo100Ask}
                  alt=""
                  className="-ml-4 hidden h-[32px] dark:block"
                />
                <img src={Logo100Ask} alt="" className="-ml-4 h-[32px] dark:hidden" />
              </div>

              <div className="space-y-2 text-center">
                <h1 className="text-4xl font-semibold text-black dark:text-white">
                  {$at("Welcome back to Smart Desktop Mimi")}
                </h1>
                <p className="font-medium text-slate-600 dark:text-[#ffffff]">
                  {$at("Enter your password to access Smart Desktop Mimi.")}
                </p>
              </div>

              <Fieldset className="space-y-12">
                <Form method="POST" className="mx-auto max-w-sm space-y-4">
                  <input
                    type="text"
                    name="username"
                    value="local-device"
                    autoComplete="username"
                    readOnly
                    aria-hidden="true"
                    className="hidden"
                  />
                  <div className="space-y-4">
                    <InputFieldWithLabel
                      label={$at("Password")}
                      type={showPassword ? "text" : "password"}
                      name="password"
                      placeholder={$at("Please enter a password")}
                      autoComplete="current-password"
                      autoFocus
                      error={actionData?.error ? $at(actionData.error) : undefined}
                      TrailingElm={
                        showPassword ? (
                          <div
                            onClick={() => setShowPassword(false)}
                            className="pointer-events-auto"
                          >
                            <LuEye className="h-4 w-4 cursor-pointer text-slate-500 dark:text-[#ffffff]" />
                          </div>
                        ) : (
                          <div
                            onClick={() => setShowPassword(true)}
                            className="pointer-events-auto"
                          >
                            <LuEyeOff className="h-4 w-4 cursor-pointer text-slate-500 dark:text-[#ffffff]" />
                          </div>
                        )
                      }
                    />
                  </div>

                  <Button
                    size="LG"
                    theme="primary"
                    fullWidth
                    type="submit"
                    text={$at("Log In")}
                    textAlign="center"
                  />

                  <div className="mt-4 flex justify-start text-xs text-slate-500 dark:text-[#ffffff]">
                    <ExtLink
                      href="https://100ask.net/"
                      className="hover:underline"
                    >
                      {$at("Forgot password?")}
                    </ExtLink>
                  </div>
                </Form>
              </Fieldset>
            </div>
          </div>
        </Container>
      </div>
    </>
  );
}

LoginLocalRoute.loader = loader;
LoginLocalRoute.action = action;
