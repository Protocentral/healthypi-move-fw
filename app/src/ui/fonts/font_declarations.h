/* Modern Google Fonts for HealthyPi Move - LVGL Font Declarations */
/* Generated fonts for AMOLED circular display optimization */

// Phase 1: Core System Fonts (Essential)
LV_FONT_DECLARE(inter_regular_16);        // General UI text - Primary font
LV_FONT_DECLARE(inter_semibold_18);       // Metric values (steps, HR, SpO2)
LV_FONT_DECLARE(jetbrains_mono_regular_16); // Time display, sensor readings

// Phase 2: Additional Essential Sizes  
LV_FONT_DECLARE(inter_regular_14);        // Secondary text, labels
LV_FONT_DECLARE(inter_regular_12);        // Small text, captions, status
LV_FONT_DECLARE(jetbrains_mono_regular_24); // Large time display, main clock

/* Font Usage Guide for HealthyPi Move UI:
 * 
 * Home Screen:
 * - Time: jetbrains_mono_regular_24
 * - HR/Steps values: inter_semibold_18  
 * - Labels: inter_regular_14
 * - Status: inter_regular_12
 * 
 * Detail Screens:
 * - Titles: inter_semibold_18
 * - Main values: jetbrains_mono_regular_16
 * - Body text: inter_regular_14
 * - Buttons: inter_regular_16
 * 
 * Memory Usage (approximate):
 * - inter_regular_12: ~27KB
 * - inter_regular_14: ~32KB  
 * - inter_regular_16: ~35KB
 * - inter_semibold_18: ~40KB
 * - jetbrains_mono_regular_16: ~34KB
 * - jetbrains_mono_regular_24: ~49KB
 * Total: ~217KB for complete font system
 */