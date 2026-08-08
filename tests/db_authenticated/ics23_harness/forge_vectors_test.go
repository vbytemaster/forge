package ics23harness

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

const (
	forgePointVectorFormat = "forge.db.authenticated.point.vector.v1"
	forgeHashSchemaVersion = 3
)

var (
	valueDomain = []byte("forge.db.authenticated.value.v1")
	leafDomain  = []byte("forge.db.authenticated.leaf.v3")
	innerDomain = []byte("forge.db.authenticated.inner.v3")
)

type forgeLeafVector struct {
	Key       string  `json:"key"`
	ValueHash string  `json:"value_hash"`
	Value     *string `json:"value,omitempty"`
}

type forgeBranchVector struct {
	Height    uint16 `json:"height"`
	Size      uint64 `json:"size"`
	MinKey    string `json:"min_key"`
	MaxKey    string `json:"max_key"`
	Separator string `json:"separator"`
	LeftHash  string `json:"left_hash"`
	RightHash string `json:"right_hash"`
}

type forgeSiblingVector struct {
	Kind   string             `json:"kind"`
	Leaf   *forgeLeafVector   `json:"leaf,omitempty"`
	Branch *forgeBranchVector `json:"branch,omitempty"`
}

type forgePointStepVector struct {
	Child       string             `json:"child"`
	Height      uint16             `json:"height"`
	SubtreeSize uint64             `json:"subtree_size"`
	MinKey      string             `json:"min_key"`
	MaxKey      string             `json:"max_key"`
	Separator   string             `json:"separator"`
	Sibling     forgeSiblingVector `json:"sibling"`
}

type forgePointVector struct {
	Format     string `json:"format"`
	HashSchema int    `json:"hash_schema"`
	Domain     string `json:"domain"`
	Tree       string `json:"tree"`
	Anchor     struct {
		StateRoot string `json:"state_root"`
		StateSize uint64 `json:"state_size"`
	} `json:"anchor"`
	QueryKey string                 `json:"query_key"`
	Terminal forgeLeafVector        `json:"terminal"`
	Path     []forgePointStepVector `json:"path"`
	Expected struct {
		Exists    bool    `json:"exists"`
		Rank      uint64  `json:"rank"`
		ValueHash *string `json:"value_hash,omitempty"`
		Value     *string `json:"value,omitempty"`
	} `json:"expected"`
}

type forgeNodeMetadata struct {
	hash   [sha256.Size]byte
	height uint16
	size   uint64
	minKey []byte
	maxKey []byte
}

type forgeVerifiedPoint struct {
	exists    bool
	rank      uint64
	valueHash [sha256.Size]byte
	value     []byte
}

func decodeForgeHex(encoded string) ([]byte, error) {
	value, err := hex.DecodeString(encoded)
	if err != nil {
		return nil, fmt.Errorf("decode hex: %w", err)
	}
	return value, nil
}

func decodeForgeDigest(encoded string) ([sha256.Size]byte, error) {
	value, err := decodeForgeHex(encoded)
	if err != nil {
		return [sha256.Size]byte{}, err
	}
	if len(value) != sha256.Size {
		return [sha256.Size]byte{}, fmt.Errorf("digest has %d bytes", len(value))
	}
	return [sha256.Size]byte(value), nil
}

func writeU16(target *bytes.Buffer, value uint16) {
	var encoded [2]byte
	binary.BigEndian.PutUint16(encoded[:], value)
	target.Write(encoded[:])
}

func writeU32(target *bytes.Buffer, value uint32) {
	var encoded [4]byte
	binary.BigEndian.PutUint32(encoded[:], value)
	target.Write(encoded[:])
}

func writeU64(target *bytes.Buffer, value uint64) {
	var encoded [8]byte
	binary.BigEndian.PutUint64(encoded[:], value)
	target.Write(encoded[:])
}

func writeSized32(target *bytes.Buffer, value []byte) error {
	if uint64(len(value)) > math.MaxUint32 {
		return errors.New("value exceeds uint32 framing")
	}
	writeU32(target, uint32(len(value)))
	target.Write(value)
	return nil
}

func writeSized64(target *bytes.Buffer, value []byte) {
	writeU64(target, uint64(len(value)))
	target.Write(value)
}

func forgeHashValue(value []byte) [sha256.Size]byte {
	var preimage bytes.Buffer
	preimage.Write(valueDomain)
	writeSized64(&preimage, value)
	return sha256.Sum256(preimage.Bytes())
}

func forgeHashLeaf(domain, key []byte, valueHash [sha256.Size]byte) ([sha256.Size]byte, error) {
	var preimage bytes.Buffer
	preimage.Write(leafDomain)
	if err := writeSized32(&preimage, domain); err != nil {
		return [sha256.Size]byte{}, err
	}
	writeSized64(&preimage, key)
	preimage.Write(valueHash[:])
	return sha256.Sum256(preimage.Bytes()), nil
}

func forgeHashInner(domain []byte, height uint16, size uint64, minKey, maxKey, separator []byte,
	left, right [sha256.Size]byte) ([sha256.Size]byte, error) {
	var preimage bytes.Buffer
	preimage.Write(innerDomain)
	if err := writeSized32(&preimage, domain); err != nil {
		return [sha256.Size]byte{}, err
	}
	writeU16(&preimage, height)
	writeU64(&preimage, size)
	writeSized64(&preimage, minKey)
	writeSized64(&preimage, maxKey)
	writeSized64(&preimage, separator)
	preimage.Write(left[:])
	preimage.Write(right[:])
	return sha256.Sum256(preimage.Bytes()), nil
}

func verifyForgeLeaf(domain []byte, vector forgeLeafVector) (forgeNodeMetadata, []byte, error) {
	key, err := decodeForgeHex(vector.Key)
	if err != nil {
		return forgeNodeMetadata{}, nil, fmt.Errorf("leaf key: %w", err)
	}
	valueHash, err := decodeForgeDigest(vector.ValueHash)
	if err != nil {
		return forgeNodeMetadata{}, nil, fmt.Errorf("leaf value hash: %w", err)
	}
	var value []byte
	if vector.Value != nil {
		value, err = decodeForgeHex(*vector.Value)
		if err != nil {
			return forgeNodeMetadata{}, nil, fmt.Errorf("leaf value: %w", err)
		}
		if forgeHashValue(value) != valueHash {
			return forgeNodeMetadata{}, nil, errors.New("leaf value hash mismatch")
		}
	}
	hash, err := forgeHashLeaf(domain, key, valueHash)
	if err != nil {
		return forgeNodeMetadata{}, nil, err
	}
	return forgeNodeMetadata{hash: hash, size: 1, minKey: key, maxKey: key}, value, nil
}

func verifyForgeBranch(domain []byte, vector forgeBranchVector) (forgeNodeMetadata, error) {
	minKey, err := decodeForgeHex(vector.MinKey)
	if err != nil {
		return forgeNodeMetadata{}, fmt.Errorf("branch minimum: %w", err)
	}
	maxKey, err := decodeForgeHex(vector.MaxKey)
	if err != nil {
		return forgeNodeMetadata{}, fmt.Errorf("branch maximum: %w", err)
	}
	separator, err := decodeForgeHex(vector.Separator)
	if err != nil {
		return forgeNodeMetadata{}, fmt.Errorf("branch separator: %w", err)
	}
	if vector.Height == 0 || vector.Size < 2 || bytes.Compare(minKey, maxKey) >= 0 ||
		bytes.Compare(minKey, separator) >= 0 || bytes.Compare(maxKey, separator) < 0 {
		return forgeNodeMetadata{}, errors.New("branch metadata is invalid")
	}
	left, err := decodeForgeDigest(vector.LeftHash)
	if err != nil {
		return forgeNodeMetadata{}, fmt.Errorf("branch left hash: %w", err)
	}
	right, err := decodeForgeDigest(vector.RightHash)
	if err != nil {
		return forgeNodeMetadata{}, fmt.Errorf("branch right hash: %w", err)
	}
	hash, err := forgeHashInner(domain, vector.Height, vector.Size, minKey, maxKey, separator, left, right)
	if err != nil {
		return forgeNodeMetadata{}, err
	}
	return forgeNodeMetadata{
		hash: hash, height: vector.Height, size: vector.Size, minKey: minKey, maxKey: maxKey,
	}, nil
}

func verifyForgeSibling(domain []byte, vector forgeSiblingVector) (forgeNodeMetadata, error) {
	switch vector.Kind {
	case "leaf":
		if vector.Leaf == nil || vector.Branch != nil {
			return forgeNodeMetadata{}, errors.New("leaf sibling must contain only leaf data")
		}
		metadata, _, err := verifyForgeLeaf(domain, *vector.Leaf)
		return metadata, err
	case "branch":
		if vector.Branch == nil || vector.Leaf != nil {
			return forgeNodeMetadata{}, errors.New("branch sibling must contain only branch data")
		}
		return verifyForgeBranch(domain, *vector.Branch)
	default:
		return forgeNodeMetadata{}, fmt.Errorf("unknown sibling kind %q", vector.Kind)
	}
}

func combineForgeNodes(domain []byte, step forgePointStepVector, left, right forgeNodeMetadata) (forgeNodeMetadata, error) {
	minKey, err := decodeForgeHex(step.MinKey)
	if err != nil {
		return forgeNodeMetadata{}, fmt.Errorf("step minimum: %w", err)
	}
	maxKey, err := decodeForgeHex(step.MaxKey)
	if err != nil {
		return forgeNodeMetadata{}, fmt.Errorf("step maximum: %w", err)
	}
	separator, err := decodeForgeHex(step.Separator)
	if err != nil {
		return forgeNodeMetadata{}, fmt.Errorf("step separator: %w", err)
	}
	if left.size > math.MaxUint64-right.size || step.SubtreeSize != left.size+right.size {
		return forgeNodeMetadata{}, errors.New("step subtree size is inconsistent")
	}
	expectedHeight := uint32(max(left.height, right.height)) + 1
	if expectedHeight > math.MaxUint16 || step.Height != uint16(expectedHeight) ||
		abs(int(left.height)-int(right.height)) > 1 {
		return forgeNodeMetadata{}, errors.New("step AVL metadata is inconsistent")
	}
	if bytes.Compare(left.maxKey, right.minKey) >= 0 || !bytes.Equal(separator, right.minKey) ||
		!bytes.Equal(minKey, left.minKey) || !bytes.Equal(maxKey, right.maxKey) {
		return forgeNodeMetadata{}, errors.New("step ordered bounds are inconsistent")
	}
	hash, err := forgeHashInner(domain, step.Height, step.SubtreeSize, minKey, maxKey, separator,
		left.hash, right.hash)
	if err != nil {
		return forgeNodeMetadata{}, err
	}
	return forgeNodeMetadata{
		hash: hash, height: step.Height, size: step.SubtreeSize, minKey: minKey, maxKey: maxKey,
	}, nil
}

func verifyForgePoint(vector forgePointVector) (forgeVerifiedPoint, error) {
	if vector.Format != forgePointVectorFormat || vector.HashSchema != forgeHashSchemaVersion {
		return forgeVerifiedPoint{}, errors.New("unsupported Forge point vector format")
	}
	if vector.Domain == "" || vector.Tree != "state" {
		return forgeVerifiedPoint{}, errors.New("point vector requires a state tree domain")
	}
	treeDomain := append([]byte{1}, []byte(vector.Domain)...)
	queryKey, err := decodeForgeHex(vector.QueryKey)
	if err != nil {
		return forgeVerifiedPoint{}, fmt.Errorf("query key: %w", err)
	}
	current, terminalValue, err := verifyForgeLeaf(treeDomain, vector.Terminal)
	if err != nil {
		return forgeVerifiedPoint{}, fmt.Errorf("terminal: %w", err)
	}
	terminalKey := append([]byte(nil), current.minKey...)
	rank := uint64(0)
	if bytes.Compare(current.minKey, queryKey) < 0 {
		rank = 1
	}
	for index, step := range vector.Path {
		separator, err := decodeForgeHex(step.Separator)
		if err != nil {
			return forgeVerifiedPoint{}, fmt.Errorf("path[%d] separator: %w", index, err)
		}
		goLeft := bytes.Compare(queryKey, separator) < 0
		if (step.Child == "left") != goLeft || (step.Child != "left" && step.Child != "right") {
			return forgeVerifiedPoint{}, fmt.Errorf("path[%d] follows the wrong search branch", index)
		}
		sibling, err := verifyForgeSibling(treeDomain, step.Sibling)
		if err != nil {
			return forgeVerifiedPoint{}, fmt.Errorf("path[%d] sibling: %w", index, err)
		}
		if step.Child == "left" {
			current, err = combineForgeNodes(treeDomain, step, current, sibling)
		} else {
			if rank > math.MaxUint64-sibling.size {
				return forgeVerifiedPoint{}, errors.New("point rank overflows")
			}
			rank += sibling.size
			current, err = combineForgeNodes(treeDomain, step, sibling, current)
		}
		if err != nil {
			return forgeVerifiedPoint{}, fmt.Errorf("path[%d]: %w", index, err)
		}
	}
	expectedRoot, err := decodeForgeDigest(vector.Anchor.StateRoot)
	if err != nil {
		return forgeVerifiedPoint{}, fmt.Errorf("anchor root: %w", err)
	}
	if current.hash != expectedRoot || current.size != vector.Anchor.StateSize {
		return forgeVerifiedPoint{}, errors.New("proof does not reconstruct the anchor")
	}
	exists := bytes.Equal(terminalKey, queryKey)
	result := forgeVerifiedPoint{exists: exists, rank: rank}
	if exists {
		result.valueHash, err = decodeForgeDigest(vector.Terminal.ValueHash)
		if err != nil {
			return forgeVerifiedPoint{}, err
		}
		result.value = terminalValue
	}
	return result, nil
}

func validateForgePointExpectation(vector forgePointVector, actual forgeVerifiedPoint) error {
	if actual.exists != vector.Expected.Exists || actual.rank != vector.Expected.Rank {
		return fmt.Errorf("result mismatch: exists=%v rank=%d", actual.exists, actual.rank)
	}
	if !actual.exists {
		if vector.Expected.ValueHash != nil || vector.Expected.Value != nil {
			return errors.New("non-membership vector must not expect a value")
		}
		return nil
	}
	if vector.Expected.ValueHash == nil {
		return errors.New("membership vector omits expected value hash")
	}
	expectedHash, err := decodeForgeDigest(*vector.Expected.ValueHash)
	if err != nil {
		return err
	}
	if actual.valueHash != expectedHash {
		return errors.New("verified value hash mismatch")
	}
	if vector.Expected.Value != nil {
		expectedValue, err := decodeForgeHex(*vector.Expected.Value)
		if err != nil {
			return err
		}
		if !bytes.Equal(actual.value, expectedValue) {
			return errors.New("verified value mismatch")
		}
	}
	return nil
}

func TestForgeV3PointProofVectors(t *testing.T) {
	_, source, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("locate harness source")
	}
	for _, name := range []string{"forge_point_membership_v3.json", "forge_point_nonmembership_v3.json"} {
		name := name
		t.Run(name, func(t *testing.T) {
			encoded, err := os.ReadFile(filepath.Join(filepath.Dir(source), "vectors", name))
			if err != nil {
				t.Fatalf("read vector: %v", err)
			}
			var vector forgePointVector
			decoder := json.NewDecoder(bytes.NewReader(encoded))
			decoder.DisallowUnknownFields()
			if err := decoder.Decode(&vector); err != nil {
				t.Fatalf("decode vector: %v", err)
			}
			if err := decoder.Decode(&struct{}{}); err != io.EOF {
				t.Fatalf("vector has trailing JSON: %v", err)
			}
			verified, err := verifyForgePoint(vector)
			if err != nil {
				t.Fatalf("verify Forge point proof: %v", err)
			}
			if err := validateForgePointExpectation(vector, verified); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func max(left, right uint16) uint16 {
	if left > right {
		return left
	}
	return right
}

func abs(value int) int {
	if value < 0 {
		return -value
	}
	return value
}
