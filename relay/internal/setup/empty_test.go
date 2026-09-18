package setup

import "testing"

// Empty store (fresh clone, no .env) renders the keys pointer.
func TestEmptyState(t *testing.T) {
	s := NewStore("/nonexistent/.env")
	if len(s.Load()) != 0 {
		t.Fatal("want empty vals for missing file")
	}
}
