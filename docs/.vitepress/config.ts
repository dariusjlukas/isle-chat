import { defineConfig } from "vitepress";

export default defineConfig({
  title: "EnclaveStation",
  description: "Documentation for EnclaveStation — a self-hosted chat platform",
  cleanUrls: true,

  head: [
    ["meta", { name: "theme-color", content: "#4f46e5" }],
    ["link", { rel: "icon", href: "/favicon.ico" }],
  ],

  themeConfig: {
    logo: {
      light: "/logo-light.png",
      dark: "/logo-dark.png",
    },

    nav: [
      { text: "Home", link: "/" },
      { text: "Guides", link: "/guides/local-deployment" },
    ],

    sidebar: [
      {
        text: "Deployment Guides",
        items: [
          {
            text: "Deploy Locally",
            link: "/guides/local-deployment",
          },
          {
            text: "Deploy on AWS Lightsail",
            link: "/guides/lightsail-deployment",
          },
          {
            text: "Deploy on AWS EC2",
            link: "/guides/ec2-deployment",
          },
          {
            text: "Deploy on Azure",
            link: "/guides/azure-deployment",
          },
          {
            text: "Deploy on Google Cloud",
            link: "/guides/gcp-deployment",
          },
        ],
      },
    ],

    socialLinks: [
      {
        icon: "github",
        link: "https://github.com/dariusjlukas/enclave-station",
      },
    ],

    footer: {
      message: "Released under the Apache-2.0 License.",
    },

    search: {
      provider: "local",
    },
  },
});
