# Security model

The TCP signaling protocol does not authenticate peers or encrypt signaling. WebRTC DTLS/SRTP protects media in transit, but it does not authenticate a peer if an attacker can alter the separate signaling exchange.

**Use multiplayer only with trusted peers over a trusted LAN or private VPN. Do not expose the signaling listener to arbitrary Internet peers or public port-forwarding.** Do not commit TURN credentials, tokens, private configuration, SDP dumps or unnecessary IP addresses to the repository or logs.

Before treating this as Internet-ready, the project needs authenticated peer identity and signaling, explicit authorization for shared windows and input/control messages, rate limits and size limits, credential redaction, protocol versioning, and reconnect/disconnect security tests. These are known limitations, not implemented guarantees.
