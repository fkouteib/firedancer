#!/bin/bash
set -euxo pipefail
IFS=$'\n\t'

cd ../test-ledger

RPC_URL="http://localhost:8899/"
AGAVE_PATH=${AGAVE_PATH:='./agave/target/release'}

$AGAVE_PATH/solana-keygen new --no-bip39-passphrase --silent --outfile fd-identity-keypair.json
$AGAVE_PATH/solana-keygen new --no-bip39-passphrase --silent --outfile fd-stake-keypair.json
$AGAVE_PATH/solana-keygen new --no-bip39-passphrase --silent --outfile fd-vote-keypair.json
$AGAVE_PATH/solana-keygen new --no-bip39-passphrase --silent --outfile fd-withdrawer-keypair.json

$AGAVE_PATH/solana -u $RPC_URL --keypair faucet-keypair.json transfer --allow-unfunded-recipient fd-identity-keypair.json 400000
$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair.json create-vote-account fd-vote-keypair.json fd-identity-keypair.json fd-withdrawer-keypair.json
$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair.json create-stake-account fd-stake-keypair.json 300000
$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair.json delegate-stake fd-stake-keypair.json fd-vote-keypair.json

$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair.json vote-account fd-vote-keypair.json
$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair.json stake-account fd-stake-keypair.json

exit 0
$AGAVE_PATH/solana-keygen new --no-bip39-passphrase --silent --outfile fd-identity-keypair-2.json
$AGAVE_PATH/solana-keygen new --no-bip39-passphrase --silent --outfile fd-stake-keypair-2.json
$AGAVE_PATH/solana-keygen new --no-bip39-passphrase --silent --outfile fd-vote-keypair-2.json
$AGAVE_PATH/solana-keygen new --no-bip39-passphrase --silent --outfile fd-withdrawer-keypair-2.json

$AGAVE_PATH/solana -u $RPC_URL --keypair faucet-keypair.json transfer --allow-unfunded-recipient fd-identity-keypair-2.json 4000000
$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair-2.json create-vote-account fd-vote-keypair-2.json fd-identity-keypair-2.json fd-withdrawer-keypair-2.json
$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair-2.json create-stake-account fd-stake-keypair-2.json 100000
$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair-2.json delegate-stake fd-stake-keypair-2.json fd-vote-keypair-2.json

$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair-2.json vote-account fd-vote-keypair-2.json
$AGAVE_PATH/solana -u $RPC_URL --keypair fd-identity-keypair-2.json stake-account fd-stake-keypair-2.json
