// Railway settings of the bagi-chess service, the web build of the game (Infrastructure as Code; Railway reads no railway.json after
// 2026-12-01). The partial "chess-web" owns only this service: apply leaves the other services of the project alone. Variables
// (BASIC_AUTH_*) stay unmanaged: `railway variables`, never here. The domain is set on the service, not in this file.
//   cd web && npm install && railway config plan && railway config apply
// The deploy itself: README.md, "Browser build" (engine/tools/web/build_and_deploy.py --deploy). The image is engine/tools/web/Dockerfile,
// staged by build_and_deploy.py; its CMD starts serve.py, so there is no start command here. No volume: Chess posts no match logs.
import { defineRailway, project, service } from "railway/iac";

export const partial = "chess-web";

export default defineRailway(() => {
  const web = service("bagi-chess", {
    build: { builder: "DOCKERFILE", dockerfilePath: "Dockerfile" },
    deploy: {
      restartPolicyType: "ON_FAILURE",
      restartPolicyMaxRetries: 5,
    },
    healthcheck: "/healthz",
    healthcheckTimeout: 60,
    replicas: { sfo: 1 },
  });

  return project("games and servers", {
    variables: { managed: false },
    resources: [web],
  });
});
