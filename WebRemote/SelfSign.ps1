# This script generates a self-signed certificate with the specified IP addresses as SAN entries.
# Usage:
# .\SelfSign.ps1 -IPs "192.168.1.1","192.168.1.2"
param(
    [string[]]$IPs
)

# Always include DNS:localhost
$sanItems = @("DNS:localhost")

# Add all IP parameters
foreach ($ip in $IPs) {
    $sanItems += "IP:$ip"
}

# Join all SAN items into a single string
$san = $sanItems -join ","

openssl req -x509 -newkey rsa:2048 -nodes `
  -keyout key.pem `
  -out cert.pem `
  -days 365 `
  -subj "/CN=localhost" `
  -addext "subjectAltName=$san"