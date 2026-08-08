package ics23harness

import (
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"testing"

	ics23 "github.com/cosmos/ics23/go"
)

type vector struct {
	Key   string `json:"key"`
	Proof string `json:"proof"`
	Root  string `json:"root"`
	Value string `json:"value"`
}

func decodeHex(t *testing.T, encoded string) []byte {
	t.Helper()
	value, err := hex.DecodeString(encoded)
	if err != nil {
		t.Fatalf("decode vector hex: %v", err)
	}
	return value
}

func TestOfficialIAVLProofVectors(t *testing.T) {
	_, source, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("locate harness source")
	}
	for _, name := range []string{"exist_middle.json", "nonexist_middle.json"} {
		name := name
		t.Run(name, func(t *testing.T) {
			encoded, err := os.ReadFile(filepath.Join(filepath.Dir(source), "vectors", name))
			if err != nil {
				t.Fatalf("read vector: %v", err)
			}
			var fixture vector
			if err := json.Unmarshal(encoded, &fixture); err != nil {
				t.Fatalf("decode vector: %v", err)
			}

			proof := &ics23.CommitmentProof{}
			if err := proof.Unmarshal(decodeHex(t, fixture.Proof)); err != nil {
				t.Fatalf("decode commitment proof: %v", err)
			}
			root := decodeHex(t, fixture.Root)
			key := decodeHex(t, fixture.Key)
			if fixture.Value == "" {
				if !ics23.VerifyNonMembership(ics23.IavlSpec, root, proof, key) {
					t.Fatal("official non-existence vector was rejected")
				}
				return
			}
			if !ics23.VerifyMembership(
				ics23.IavlSpec, root, proof, key, decodeHex(t, fixture.Value)) {
				t.Fatal("official existence vector was rejected")
			}
		})
	}
}
