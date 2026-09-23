package kvm

import (
	"context"
	"time"

	"github.com/coder/websocket/wsjson"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promauto"

	"github.com/coder/websocket"
	"github.com/gin-gonic/gin"
	"github.com/rs/zerolog"
)

type CloudRegisterRequest struct {
	Token      string `json:"token"`
	CloudAPI   string `json:"cloudApi"`
	OidcGoogle string `json:"oidcGoogle"`
	ClientId   string `json:"clientId"`
}

const (
	// CloudWebSocketConnectTimeout is the timeout for the websocket connection to the cloud
	CloudWebSocketConnectTimeout = 1 * time.Minute
	// CloudAPIRequestTimeout is the timeout for cloud API requests
	CloudAPIRequestTimeout = 10 * time.Second
	// CloudOidcRequestTimeout is the timeout for OIDC token verification requests
	// should be lower than the websocket response timeout set in cloud-api
	CloudOidcRequestTimeout = 10 * time.Second
	// WebsocketPingInterval is the interval at which the websocket client sends ping messages to the cloud
	WebsocketPingInterval = 15 * time.Second
)

var (
	metricConnectionLastPingTimestamp = promauto.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "kvm_connection_last_ping_timestamp_seconds",
			Help: "The timestamp when the last ping response was received",
		},
		[]string{"type", "source"},
	)
	metricConnectionLastPingReceivedTimestamp = promauto.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "kvm_connection_last_ping_received_timestamp_seconds",
			Help: "The timestamp when the last ping request was received",
		},
		[]string{"type", "source"},
	)
	metricConnectionLastPingDuration = promauto.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "kvm_connection_last_ping_duration_seconds",
			Help: "The duration of the last ping response",
		},
		[]string{"type", "source"},
	)
	metricConnectionPingDuration = promauto.NewHistogramVec(
		prometheus.HistogramOpts{
			Name: "kvm_connection_ping_duration_seconds",
			Help: "The duration of the ping response",
			Buckets: []float64{
				0.1, 0.5, 1, 10,
			},
		},
		[]string{"type", "source"},
	)
	metricConnectionTotalPingSentCount = promauto.NewCounterVec(
		prometheus.CounterOpts{
			Name: "kvm_connection_ping_sent_total",
			Help: "The total number of pings sent to the connection",
		},
		[]string{"type", "source"},
	)
	metricConnectionTotalPingReceivedCount = promauto.NewCounterVec(
		prometheus.CounterOpts{
			Name: "kvm_connection_ping_received_total",
			Help: "The total number of pings received from the connection",
		},
		[]string{"type", "source"},
	)
	metricConnectionSessionRequestCount = promauto.NewCounterVec(
		prometheus.CounterOpts{
			Name: "kvm_connection_session_requests_total",
			Help: "The total number of session requests received",
		},
		[]string{"type", "source"},
	)
	metricConnectionSessionRequestDuration = promauto.NewHistogramVec(
		prometheus.HistogramOpts{
			Name: "kvm_connection_session_request_duration_seconds",
			Help: "The duration of session requests",
			Buckets: []float64{
				0.1, 0.5, 1, 10,
			},
		},
		[]string{"type", "source"},
	)
	metricConnectionLastSessionRequestTimestamp = promauto.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "kvm_connection_last_session_request_timestamp_seconds",
			Help: "The timestamp of the last session request",
		},
		[]string{"type", "source"},
	)
	metricConnectionLastSessionRequestDuration = promauto.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "kvm_connection_last_session_request_duration",
			Help: "The duration of the last session request",
		},
		[]string{"type", "source"},
	)
)

func handleSessionRequest(
	ctx context.Context,
	c *websocket.Conn,
	req WebRTCSessionRequest,
	source string,
	scopedLogger *zerolog.Logger,
) (*Session, error) {
	var sourceType = "local"

	timer := prometheus.NewTimer(prometheus.ObserverFunc(func(v float64) {
		metricConnectionLastSessionRequestDuration.WithLabelValues(sourceType, source).Set(v)
		metricConnectionSessionRequestDuration.WithLabelValues(sourceType, source).Observe(v)
	}))
	defer timer.ObserveDuration()

	session, err := newSession(SessionConfig{
		ws:         c,
		LocalIP:    req.IP,
		ICEServers: req.ICEServers,
		Logger:     scopedLogger,
	})
	if err != nil {
		_ = wsjson.Write(context.Background(), c, gin.H{"error": err})
		return nil, err
	}

	sd, err := session.ExchangeOffer(req.Sd)
	if err != nil {
		_ = wsjson.Write(context.Background(), c, gin.H{"error": err})
		return nil, err
	}
	if previous := replaceCurrentSession(session); previous != nil {
		writeJSONRPCEvent("otherSessionConnected", nil, previous)
		peerConn := previous.peerConnection
		go func() {
			time.Sleep(1 * time.Second)
			_ = peerConn.Close()
		}()
	}

	cloudLogger.Info().Interface("session", session).Msg("new session accepted")
	cloudLogger.Trace().Interface("session", session).Msg("new session accepted")
	if err := wsjson.Write(context.Background(), c, gin.H{"type": "answer", "data": sd}); err != nil {
		clearCurrentSession(session)
		_ = session.peerConnection.Close()
		return nil, err
	}
	return session, nil
}
