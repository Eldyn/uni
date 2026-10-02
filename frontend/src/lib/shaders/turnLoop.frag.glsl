// The play-direction loop: hard-edged pixel chevrons joined by a dashed
// track, running round a rounded rectangle on the felt. Everything is worked
// in the felt's art-pixel block space (x right, y up) so the loop sits on the
// same pixel grid as the felt, and each block is coloured by the felt's own
// dithered ripple (matRippleColor), lightened toward white so it reads on a
// felt of the same colour. Mirrors loopGeometry.ts's outlineParam.
uniform vec2 uLoopCenter;
uniform vec2 uLoopHalfSize;
uniform float uLoopCornerRadius;
uniform float uLoopLength;
uniform float uPitch;
uniform float uPhase;
uniform float uDirection;
uniform float uLighten;
uniform float uChevronLength;
uniform float uChevronHalfSpread;
uniform float uStroke;
uniform float uDashLength;
uniform float uDashPitch;
uniform float uDashHalfWidth;
uniform float uDashClearance;

varying vec2 vUv;

const float PI = 3.141592653589793;

float segmentDistance(vec2 point, vec2 start, vec2 end) {
	vec2 segment = end - start;
	float t = clamp(dot(point - start, segment) / dot(segment, segment), 0.0, 1.0);
	return length(point - start - segment * t);
}

// vec2(s, across): arc length along the outline (counter-clockwise from the
// bottom-edge midpoint) and signed distance from it (positive outside).
vec2 outlineParam(vec2 offset) {
	float radius = uLoopCornerRadius;
	float straightHalfWidth = uLoopHalfSize.x - radius;
	float straightHalfHeight = uLoopHalfSize.y - radius;
	float quarterArc = PI * radius * 0.5;
	vec2 absOffset = abs(offset);
	vec2 outside = max(absOffset - vec2(straightHalfWidth, straightHalfHeight), 0.0);
	float across = length(outside)
		+ min(max(absOffset.x - straightHalfWidth, absOffset.y - straightHalfHeight), 0.0)
		- radius;

	float rightEdgeStart = straightHalfWidth + quarterArc;
	float topRightArcStart = rightEdgeStart + 2.0 * straightHalfHeight;
	float topEdgeStart = topRightArcStart + quarterArc;
	float topLeftArcStart = topEdgeStart + 2.0 * straightHalfWidth;
	float leftEdgeStart = topLeftArcStart + quarterArc;
	float bottomLeftArcStart = leftEdgeStart + 2.0 * straightHalfHeight;

	float arcLength;
	if (offset.x > straightHalfWidth && offset.y < -straightHalfHeight) {
		arcLength = straightHalfWidth
			+ radius * (atan(offset.y + straightHalfHeight, offset.x - straightHalfWidth) + PI * 0.5);
	} else if (offset.x > straightHalfWidth && offset.y > straightHalfHeight) {
		arcLength = topRightArcStart
			+ radius * atan(offset.y - straightHalfHeight, offset.x - straightHalfWidth);
	} else if (offset.x < -straightHalfWidth && offset.y > straightHalfHeight) {
		arcLength = topLeftArcStart
			+ radius * (atan(offset.y - straightHalfHeight, offset.x + straightHalfWidth) - PI * 0.5);
	} else if (offset.x < -straightHalfWidth && offset.y < -straightHalfHeight) {
		arcLength = bottomLeftArcStart
			+ radius * (atan(offset.y + straightHalfHeight, offset.x + straightHalfWidth) + PI);
	} else if (absOffset.x - straightHalfWidth >= absOffset.y - straightHalfHeight) {
		arcLength = offset.x > 0.0
			? rightEdgeStart + offset.y + straightHalfHeight
			: leftEdgeStart + straightHalfHeight - offset.y;
	} else if (offset.y > 0.0) {
		arcLength = topEdgeStart + straightHalfWidth - offset.x;
	} else {
		arcLength = offset.x >= 0.0 ? offset.x : uLoopLength + offset.x;
	}
	return vec2(arcLength, across);
}

void main() {
	vec2 blockPosition = floor(vUv * uBlockCount) + 0.5;
	vec2 param = outlineParam(blockPosition - uLoopCenter);
	float arcLength = param.x;
	float across = param.y;

	if (abs(across) > uChevronHalfSpread + 0.5) discard;

	// Position inside the current cell, mirrored when play runs the other way
	// so the chevron apex always leads. The phase is whole blocks: the pattern
	// crawls pixel by pixel.
	float cellOffset = mod(arcLength - floor(uPhase), uPitch);
	float along = (cellOffset - uPitch * 0.5) * uDirection;

	float apex = uChevronLength * 0.5;
	vec2 point = vec2(along, across);
	float upperArm = segmentDistance(point, vec2(-apex, uChevronHalfSpread), vec2(apex, 0.0));
	float lowerArm = segmentDistance(point, vec2(-apex, -uChevronHalfSpread), vec2(apex, 0.0));
	bool onChevron = min(upperArm, lowerArm) <= uStroke * 0.5;

	float trackAlong = abs(along) - uDashClearance;
	bool onTrack = trackAlong >= 0.0
		&& abs(across) <= uDashHalfWidth
		&& mod(trackAlong, uDashPitch) < uDashLength;

	if (!onChevron && !onTrack) discard;

	vec3 color = mix(matRippleColor(vUv), vec3(1.0), uLighten);
	gl_FragColor = vec4(color, 1.0);
	#include <colorspace_fragment>
}
