package setup

import "net/url"

func parseURL(v string) (string, error) {
	if _, err := url.ParseRequestURI(v); err != nil {
		return "", err
	}
	return v, nil
}
