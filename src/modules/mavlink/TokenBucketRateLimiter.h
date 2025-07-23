// In mavlink_main.h
class Mavlink
{
	// ... existing code ...

private:
	class TokenBucketRateLimiter
	{
	private:
		hrt_abstime _last_token_update{0};
		float _available_tokens{0.0f};      // Currently available tokens (in bytes)
		float _max_bucket_tokens{0.0f};     // Maximum tokens bucket can hold
		float _tokens_per_microsecond{0.0f}; // Token generation rate
		uint32_t _configured_rate_bytes_per_sec{0}; // For debugging/telemetry

		// Statistics
		uint64_t _total_tokens_requested{0};
		uint64_t _total_tokens_granted{0};
		uint64_t _total_tokens_denied{0};

	public:
		/**
		 * Configure the rate limiter
		 * @param bytes_per_sec Maximum data rate in bytes per second
		 */
		void configure_rate(uint32_t bytes_per_sec)
		{
			_configured_rate_bytes_per_sec = bytes_per_sec;
			_tokens_per_microsecond = static_cast<float>(bytes_per_sec) / 1e6f;

			// TODO: how large of a burst should we allow? Probably MAVLINK_MAX_PACKET_LEN

			// Bucket size: allow burst of 100ms worth of data
			// This permits parameter sending bursts while maintaining average rate
			_max_bucket_tokens = static_cast<float>(bytes_per_sec) * 0.1f;

			// Don't let available tokens exceed new limit
			if (_available_tokens > _max_bucket_tokens) {
				_available_tokens = _max_bucket_tokens;
			}
		}

		/**
		 * Request tokens for transmission
		 * @param bytes Number of bytes to transmit
		 * @param priority Allow overdraft for high priority messages
		 * @return true if tokens were granted
		 */
		bool request_tokens(size_t bytes, bool priority = false)
		{
			replenish_tokens();

			_total_tokens_requested += bytes;

			const float tokens_needed = static_cast<float>(bytes);

			if (priority && _available_tokens < tokens_needed) {
				// Priority messages can overdraft up to 20% of bucket size
				const float max_overdraft = _max_bucket_tokens * 0.2f;

				if (_available_tokens >= -max_overdraft) {
					_available_tokens -= tokens_needed;
					_total_tokens_granted += bytes;
					return true;
				}
			}

			if (_available_tokens >= tokens_needed) {
				_available_tokens -= tokens_needed;
				_total_tokens_granted += bytes;
				return true;
			}

			_total_tokens_denied += bytes;
			return false;
		}

		/**
		 * Check if tokens are available without consuming them
		 * @param bytes Number of bytes to check
		 * @return true if tokens would be available
		 */
		bool check_tokens_available(size_t bytes) const
		{
			return _available_tokens >= static_cast<float>(bytes);
		}

		/**
		 * Get current available tokens
		 * @return Available tokens in bytes
		 */
		float get_available_tokens()
		{
			replenish_tokens();
			return _available_tokens;
		}

		/**
		 * Get percentage of bucket filled
		 * @return 0.0 to 1.0 representing bucket fill level
		 */
		float get_bucket_fill_ratio()
		{
			replenish_tokens();
			return _available_tokens / _max_bucket_tokens;
		}

		/**
		 * Force token replenishment (useful for testing)
		 */
		void force_replenish()
		{
			replenish_tokens();
		}

		/**
		 * Get rate limiter statistics
		 */
		struct Statistics {
			uint64_t tokens_requested;
			uint64_t tokens_granted;
			uint64_t tokens_denied;
			float current_tokens;
			float max_tokens;
			float fill_ratio;
			uint32_t configured_rate;
		};

		Statistics get_statistics()
		{
			replenish_tokens();
			return {
				_total_tokens_requested,
				_total_tokens_granted,
				_total_tokens_denied,
				_available_tokens,
				_max_bucket_tokens,
				get_bucket_fill_ratio(),
				_configured_rate_bytes_per_sec
			};
		}

	private:
		void replenish_tokens()
		{
			const hrt_abstime now = hrt_absolute_time();

			// Handle first call
			if (_last_token_update == 0) {
				_last_token_update = now;
				_available_tokens = _max_bucket_tokens;
				return;
			}

			const float dt_us = static_cast<float>(now - _last_token_update);
			_last_token_update = now;

			// Generate new tokens based on elapsed time
			const float new_tokens = _tokens_per_microsecond * dt_us;
			_available_tokens += new_tokens;

			// Cap at bucket maximum
			if (_available_tokens > _max_bucket_tokens) {
				_available_tokens = _max_bucket_tokens;
			}
		}
	};

	TokenBucketRateLimiter _tx_rate_limiter;

public:
	/**
	 * Request tokens for data transmission
	 * @param bytes Number of bytes to transmit
	 * @param priority High priority messages can overdraft
	 * @return true if transmission is allowed
	 */
	bool request_data_tokens(size_t bytes, bool priority = false)
	{
		return _tx_rate_limiter.request_tokens(bytes, priority);
	}

	/**
	 * Check if tokens available without consuming
	 * @param bytes Number of bytes to check
	 * @return true if tokens available
	 */
	bool check_data_tokens(size_t bytes) const
	{
		return _tx_rate_limiter.check_tokens_available(bytes);
	}

	/**
	 * Get current token availability
	 * @return Available tokens in bytes
	 */
	float get_available_data_tokens()
	{
		return _tx_rate_limiter.get_available_tokens();
	}

	/**
	 * Update rate limiter when configuration changes
	 */
	void update_data_rate_limits()
	{
		// Use the configured data rate
		_tx_rate_limiter.configure_rate(_datarate);
	}
};
