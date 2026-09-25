# Security notes

## Third-party scripts and SRI

`frontend/index.html` loads Google AdSense (`pagead2.googlesyndication.com`) and
gtag (`www.googletagmanager.com`) from Google's CDNs. Both scripts are
versionless and change server-side, so Subresource Integrity hashes cannot be
maintained. They are instead constrained by the Traefik CSP
(`uni-infra/oci/docker-compose.yml`, `sec-headers` middleware), which allowlists
only `googletagmanager.com`, `google-analytics.com` and the AdSense CDNs.

If a script is ever self-hosted, add an `integrity="sha384-..."` attribute.

## CSP rollout status

The CSP in `uni-infra/oci/docker-compose.yml` (`sec-headers` middleware) is
currently shipped as **Report-Only**
(`headers.contentSecurityPolicyReportOnly`), so violations are reported but not
blocked. This is deliberate: this policy is new and its observation window is
deploy-gated, so it must collect violation reports across a full release cycle
before it can safely block traffic.

**Follow-up (deferred):** after one release cycle with no unexpected
violations, rename `headers.contentSecurityPolicyReportOnly` to
`headers.contentSecurityPolicy` in `uni-infra/oci/docker-compose.yml` to enforce
the policy. Do not flip to enforcement before that window is clean.
