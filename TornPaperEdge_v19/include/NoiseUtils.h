/*
    NoiseUtils.h
    
    Noise generation utilities for torn paper edge effect.
    Uses multiple octaves of noise for organic-looking tears.
*/

#pragma once

#ifndef NOISEUTILS_H
#define NOISEUTILS_H

#include <cmath>
#include <cstdint>

// Simple but effective hash function for noise
inline uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x85ebca6b;
    x ^= x >> 13;
    x *= 0xc2b2ae35;
    x ^= x >> 16;
    return x;
}

inline uint32_t hash2D(int32_t x, int32_t y, int32_t seed) {
    return hash(hash(x + seed) ^ (y * 15731));
}

// Smooth interpolation
inline double smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

inline double lerp(double a, double b, double t) {
    return a + t * (b - a);
}

// Value noise - returns value in range [-1, 1]
inline double valueNoise2D(double x, double y, int32_t seed) {
    int32_t xi = (int32_t)floor(x);
    int32_t yi = (int32_t)floor(y);
    
    double xf = x - xi;
    double yf = y - yi;
    
    // Hash corners
    double n00 = (double)(hash2D(xi, yi, seed) & 0xFFFF) / 32768.0 - 1.0;
    double n10 = (double)(hash2D(xi + 1, yi, seed) & 0xFFFF) / 32768.0 - 1.0;
    double n01 = (double)(hash2D(xi, yi + 1, seed) & 0xFFFF) / 32768.0 - 1.0;
    double n11 = (double)(hash2D(xi + 1, yi + 1, seed) & 0xFFFF) / 32768.0 - 1.0;
    
    // Smooth interpolation
    double sx = smoothstep(xf);
    double sy = smoothstep(yf);
    
    double nx0 = lerp(n00, n10, sx);
    double nx1 = lerp(n01, n11, sx);
    
    return lerp(nx0, nx1, sy);
}

// Fractal Brownian Motion - layered noise for natural look
inline double fbm2D(double x, double y, int32_t seed, int octaves, double persistence = 0.5) {
    double total = 0.0;
    double frequency = 1.0;
    double amplitude = 1.0;
    double maxValue = 0.0;
    
    for (int i = 0; i < octaves; i++) {
        total += valueNoise2D(x * frequency, y * frequency, seed + i * 1000) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }
    
    return total / maxValue;
}

// Specialized torn paper noise - combines large tears with fine fiber detail
inline double tornPaperNoise(
    double x, 
    double y, 
    int32_t seed,
    double roughnessScale,
    double roughnessAmount,
    double detailScale,
    double detailAmount
) {
    // Large-scale tears (the main rough shape)
    double largeTear = fbm2D(x / roughnessScale, y / roughnessScale, seed, 4, 0.5) * roughnessAmount;
    
    // Fine detail (paper fiber texture)
    double fineDetail = fbm2D(x / detailScale, y / detailScale, seed + 5000, 3, 0.6) * detailAmount;
    
    return largeTear + fineDetail;
}

// Generate two different but related noise values for the two edges
// Uses offset seeds to ensure they're different but thematically similar
inline void tornPaperNoiseDouble(
    double x, 
    double y, 
    int32_t seed,
    double roughnessScale,
    double roughnessAmount,
    double detailScale,
    double detailAmount,
    double* noise1,
    double* noise2
) {
    // First edge
    *noise1 = tornPaperNoise(x, y, seed, roughnessScale, roughnessAmount, detailScale, detailAmount);
    
    // Second edge - offset seed and slightly different sampling position
    // This makes them related but distinct
    *noise2 = tornPaperNoise(x + 1000.0, y + 1000.0, seed + 10000, 
                             roughnessScale * 1.1, roughnessAmount,
                             detailScale * 0.9, detailAmount);
}

#endif // NOISEUTILS_H
