package confparser

import (
	"fmt"
	"reflect"
	"strings"

	"github.com/guregu/null/v6"
)

func splitString(s string) []string {
	if s == "" {
		return []string{}
	}

	return strings.Split(s, ",")
}

func toString(v interface{}) (string, error) {
	switch v := v.(type) {
	case string:
		return v, nil
	case null.String:
		return v.String, nil
	case []string:
		if len(v) == 0 {
			return "", nil
		}
		if len(v) == 1 {
			return v[0], nil
		}
		return strings.Join(v, ","), nil
	case []interface{}:
		if len(v) == 0 {
			return "", nil
		}
		if s, ok := v[0].(string); ok {
			return s, nil
		}
		return "", fmt.Errorf("unsupported type in slice: %T", v[0])
	}

	return "", fmt.Errorf("unsupported type: %s", reflect.TypeOf(v))
}
