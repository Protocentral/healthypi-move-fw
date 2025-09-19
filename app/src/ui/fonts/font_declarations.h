/* Modern Google Fonts for HealthyPi Move - LVGL Font Declarations */
/* Generated fonts for AMOLED circular display optimization */

// Phase 1: Core System Fonts (Essential)
LV_FONT_DECLARE(inter_semibold_24);       // General UI text - Primary font (upgraded from regular to semibold)
LV_FONT_DECLARE(inter_semibold_18);       // Legacy 18px font - kept for compatibility but styles now enforce 24px minimum for readability
LV_FONT_DECLARE(inter_semibold_80_time);  // Large minimalist time display (80px, digits only)
LV_FONT_DECLARE(jetbrains_mono_regular_16); // Time display, sensor readings

// Phase 2: Additional Essential Sizes  
LV_FONT_DECLARE(inter_regular_16);        // Secondary size for specific contexts
LV_FONT_DECLARE(inter_semibold_24); // Large time display, main clock

