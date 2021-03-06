/*            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 *                    Version 2, December 2004
 *
 *            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 *   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
 *
 *  0. You just DO WHAT THE FUCK YOU WANT TO.
 */

#include <helpers.h>
#include <siphash.h>

#include <stdlib.h>
#include <assert.h>

hash_t
siphash (const uint8_t key[16], const void* buffer, size_t length)
{
	assert(buffer);

	uint64_t v0;
	uint64_t v1;
	uint64_t v2;
	uint64_t v3;
	uint64_t k0;
	uint64_t k1;

	k0 = GET64(key, 0);
	k1 = GET64(key, 8);

	v0 = k0 ^ 0x736F6D6570736575ull;
	v1 = k1 ^ 0x646F72616E646F6Dull;
	v2 = k0 ^ 0x6C7967656E657261ull;
	v3 = k1 ^ 0x7465646279746573ull;

	#define round() \
		v0 += v1; v2 += v3; \
		v1  = ROTL64(v1, 13); v3 = ROTL64(v3, 16); \
		v1 ^= v0; v3 ^= v2; \
		v0  = ROTL64(v0, 32); \
		v2 += v1; v0 += v3; \
		v1  = ROTL64(v1, 17); v3 = ROTL64(v3, 21); \
		v1 ^= v2; v3 ^= v0; \
		v2  = ROTL64(v2, 32)

	#define rounds(n) \
		for (size_t i = 0; i < n; i++) { \
			round(); \
		}

	size_t i, blocks;
	for (i = 0, blocks = length & ~7; i < blocks; i += 8) {
		v3 ^= GET64(buffer, i);
		rounds(2);
		v0 ^= GET64(buffer, i);
	}

	#if __BYTE_ORDER_ == __ORDER_LITTLE_ENDIAN
		#define INDEX_FOR(n) n
	#elif __BYTE_ORDER__ == __ORDER_BIG__ENDIAN__
		#define INDEX_FOR(n) -(n - 6)
	#else
		#error "I don't know the size of this endian"
	#endif
	{
		uint64_t last7 = (length & 0xFFull) << 56;
		uint8_t* buf   = (uint8_t*) buffer;

		switch (length - blocks) {
			case 7: last7 |= ((uint64_t) buf[i + INDEX_FOR(6)] << 48);
			case 6: last7 |= ((uint64_t) buf[i + INDEX_FOR(5)] << 40);
			case 5: last7 |= ((uint64_t) buf[i + INDEX_FOR(4)] << 32);
			case 4: last7 |= ((uint64_t) buf[i + INDEX_FOR(3)] << 24);
			case 3: last7 |= ((uint64_t) buf[i + INDEX_FOR(2)] << 16);
			case 2: last7 |= ((uint64_t) buf[i + INDEX_FOR(1)] <<  8);
			case 1: last7 |= ((uint64_t) buf[i + INDEX_FOR(0)]);
		}

		v3 ^= last7;
		rounds(2);
		v0 ^= last7;
	}
	#undef INDEX_FOR

	v2 ^= 0xff;
	rounds(4);

	#undef round
	#undef rounds

	uint64_t hash = v0 ^ v1 ^ v2 ^ v3;

	#if HASH_BIT == 32
		return (hash >> 32) ^ (hash & 0xFFFFFFFF);
	#elif HASH_BIT == 64
		return hash;
	#else
		#error "Why can't I hold so many bits?"
	#endif
}

struct siphash_t {
	size_t length;

	int compression;
	int finalization;

	uint64_t v0;
	uint64_t v1;
	uint64_t v2;
	uint64_t v3;

	uint8_t remaining;
	uint8_t remainder[8];

	hash_t hash;
};

siphash_t*
siphash_new (void)
{
	return malloc(sizeof(siphash_t));
}

void
siphash_free (siphash_t* self)
{
	assert(self);

	free(self);
}

siphash_t*
siphash_init (siphash_t* self, const uint8_t key[16], int compression, int finalization)
{
	assert(self);

	uint64_t k0 = GET64(key, 0);
	uint64_t k1 = GET64(key, 8);

	self->v0 = k0 ^ 0x736F6D6570736575ull;
	self->v1 = k1 ^ 0x646F72616E646F6Dull;
	self->v2 = k0 ^ 0x6C7967656E657261ull;
	self->v3 = k1 ^ 0x7465646279746573ull;

	self->length = 0;

	self->compression  = compression;
	self->finalization = finalization;

	self->remaining = 0;

	self->hash = 0;

	return self;
}

siphash_t*
siphash_init_default (siphash_t* self, const uint8_t key[16])
{
	return siphash_init(self, key, 2, 4);
}

#define round(state) \
	state->v0 += state->v1; state->v2 += state->v3; \
	state->v1  = ROTL64(state->v1, 13); state->v3 = ROTL64(state->v3, 16); \
	state->v1 ^= state->v0; state->v3 ^= state->v2; \
	state->v0  = ROTL64(state->v0, 32); \
	state->v2 += state->v1; state->v0 += state->v3; \
	state->v1  = ROTL64(state->v1, 17); state->v3 = ROTL64(state->v3, 21); \
	state->v1 ^= state->v2; state->v3 ^= state->v0; \
	state->v2  = ROTL64(state->v2, 32)

#define rounds(state, n) \
	for (size_t i = 0; i < n; i++) { \
		round(state); \
	}

siphash_t*
siphash_update (siphash_t* self, const void* buffer, size_t length)
{
	assert(self);
	assert(buffer);

	if (unlikely(length == 0)) {
		return self;
	}

	self->length += length;

	if (self->remaining > 0) {
		if (length < 8 - self->remaining) {
			memcpy(self->remainder + self->remaining, buffer, length);
			self->remaining += length;

			return self;
		}

		size_t fill = 8 - self->remaining;
		memcpy(self->remainder + self->remaining, buffer, fill);

		self->v3 ^= GET64(self->remainder, 0);
		rounds(self, self->compression);
		self->v0 ^= GET64(self->remainder, 0);

		self->remaining  = 0;
		length          -= fill;
		buffer          += fill;
	}

	size_t i, blocks;
	for (i = 0, blocks = length & ~7; i < blocks; i += 8) {
		self->v3 ^= GET64(buffer, i);
		rounds(self, self->compression);
		self->v0 ^= GET64(buffer, i);
	}

	self->remaining = length - blocks;
	memcpy(self->remainder, buffer + blocks, self->remaining);

	return self;
}

siphash_t*
siphash_final (siphash_t* self)
{
	assert(self);

	if (self->remaining > 0) {
		#if __BYTE_ORDER_ == __ORDER_LITTLE_ENDIAN
			#define INDEX_FOR(n) n
		#elif __BYTE_ORDER__ == __ORDER_BIG__ENDIAN__
			#define INDEX_FOR(n) -(n - 6)
		#else
			#error "I don't know the size of this endian"
		#endif
		{
			uint64_t last7 = (self->length & 0xFFull) << 56;
			switch (self->remaining) {
				case 7: last7 |= ((uint64_t) self->remainder[INDEX_FOR(6)] << 48);
				case 6: last7 |= ((uint64_t) self->remainder[INDEX_FOR(5)] << 40);
				case 5: last7 |= ((uint64_t) self->remainder[INDEX_FOR(4)] << 32);
				case 4: last7 |= ((uint64_t) self->remainder[INDEX_FOR(3)] << 24);
				case 3: last7 |= ((uint64_t) self->remainder[INDEX_FOR(2)] << 16);
				case 2: last7 |= ((uint64_t) self->remainder[INDEX_FOR(1)] <<  8);
				case 1: last7 |= ((uint64_t) self->remainder[INDEX_FOR(0)]);
			}

			self->v3 ^= last7;
			rounds(self, self->compression);
			self->v0 ^= last7;
		}
		#undef INDEX_FOR
	}

	self->v2 ^= 0xFF;
	rounds(self, self->finalization);

	uint64_t hash = self->v0 ^ self->v1 ^ self->v2 ^ self->v3;

	#if HASH_BIT == 32
		self->hash = (hash >> 32) ^ (hash & 0xFFFFFFFF);
	#elif HASH_BIT == 64
		self->hash = hash;
	#else
		#error "Why can't I hold so many bits?"
	#endif

	return self;
}

#undef round
#undef rounds

hash_t
siphash_fetch (siphash_t* self)
{
	assert(self);

	return self->hash;
}
