# External integrations

Tuya, SWUpdate, 100ask and other external systems are adapters. Product domain
logic must not import their types. Credentials are references to protected
runtime storage; actual UUID/AuthKey/SN, signing keys and production endpoints
belong in the private `AI-DeskTopBox-ops` repository.
