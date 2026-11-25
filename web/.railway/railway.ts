// The settings of the Chess web service on Railway, as code. The upload of `railway up` does not carry them: the service is made once by
// the owner (README.md, "Browser build") and these values are applied to it there. The shape is a best reading of Railway's config as
// code, to be compared with BagiMahjong's web/.railway/railway.ts before the first use.
//
// The image is engine/tools/web/Dockerfile, staged by build_and_deploy.py; its CMD starts serve.py, so there is no start command here.
// No variable, domain or token belongs in this file (BASIC_AUTH_* are service variables set with the CLI).
export default {
  build: {
    builder: "DOCKERFILE",
    dockerfilePath: "Dockerfile",
  },
  deploy: {
    healthcheckPath: "/healthz",
    healthcheckTimeout: 60,
    restartPolicyType: "ON_FAILURE",
    restartPolicyMaxRetries: 5,
  },
};
