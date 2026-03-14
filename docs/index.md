---
layout: home

hero:
  name: EnclaveStation
  text: Self-hosted chat for teams and communities
  tagline: Private, secure, and fully under your control.
  image:
    light: /logo-light.png
    dark: /logo-dark.png
    alt: EnclaveStation
  actions:
    - theme: brand
      text: Deploy on AWS Lightsail
      link: /guides/lightsail-deployment
    - theme: alt
      text: View on GitHub
      link: https://github.com/dariusjlukas/enclave-station

features:
  - title: Passwordless Authentication
    details: PKI-based login using ECDSA keypairs stored in your browser. No passwords to leak or phish.
  - title: Multi-Device Support
    details: Link multiple devices to a single account using QR codes or tokens. Each device holds its own private key.
  - title: Real-Time Messaging
    details: WebSocket-powered messaging with spaces, channels, and direct messages.
  - title: File Sharing
    details: Built-in file browser with per-space storage, folder management, and versioning.
  - title: Easy Deployment
    details: A single docker compose up gets you running. Pre-built images published to GitHub Container Registry.
  - title: Fully Self-Hosted
    details: Your data stays on your server. No third-party dependencies, no telemetry, no vendor lock-in.
---
