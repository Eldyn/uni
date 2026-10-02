// The play-direction loop: pixel chevrons joined by a dashed track, running
// round a rounded rectangle on the felt. Everything is worked in the felt's
// art-pixel block space (x right, y up) and sampled at block centres, so the
// loop is made of whole pixels on the same grid as the felt. Each block takes
// the felt's own dithered ripple colour (matRippleColor) times a grey tone, so
// the loop reads as a shade of the mat and sweeps with it.
//
// Dashes sit on a slot grid whose pitch is a whole number of blocks and the
// rectangle is fitted (loopGeometry.ts fitLoopRect) so a whole number of cells
// tiles the outline: dash spacing never varies. Each chevron is a rigid
// sprite: its anchor follows the outline and the whole sprite turns with the
// outline's tangent as it goes round a corner, but its shape is never bent.
// Mirrors loopGeometry.ts's outlineParam and pathPoint.
uniform vec2 uLoopCenter;
uniform vec2 uLoopHalfSize;
uniform float uLoopCornerRadius;
uniform float uLoopLength;
uniform float uPhase;
uniform float uDirection;
uniform float uTone;
uniform float uSlot;
uniform float uDashLength;
uniform float uDashThickness;
uniform float uSlotsPerCell;
uniform float uChevronClearSlots;
uniform float uChevronArm;
uniform float uChevronStroke;

varying vec2 vUv;

const float PI = 3.141592653589793;

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

// vec4(x, y, directionX, directionY): the point at arc length `arcLength`
// along the outline (block space, relative to the loop centre) and the way it
// is heading.
vec4 pathPoint(float arcLength) {
	float radius = uLoopCornerRadius;
	float straightHalfWidth = uLoopHalfSize.x - radius;
	float straightHalfHeight = uLoopHalfSize.y - radius;
	float quarterArc = PI * radius * 0.5;
	float s = mod(arcLength, uLoopLength);

	float rightEdgeStart = straightHalfWidth + quarterArc;
	float topRightArcStart = rightEdgeStart + 2.0 * straightHalfHeight;
	float topEdgeStart = topRightArcStart + quarterArc;
	float topLeftArcStart = topEdgeStart + 2.0 * straightHalfWidth;
	float leftEdgeStart = topLeftArcStart + quarterArc;
	float bottomLeftArcStart = leftEdgeStart + 2.0 * straightHalfHeight;
	float bottomLeftStraightStart = bottomLeftArcStart + quarterArc;

	vec2 arcCentre;
	float startAngle;
	float along;
	if (s < straightHalfWidth) {
		return vec4(s, -uLoopHalfSize.y, 1.0, 0.0);
	} else if (s < rightEdgeStart) {
		arcCentre = vec2(straightHalfWidth, -straightHalfHeight);
		startAngle = -PI * 0.5;
		along = s - straightHalfWidth;
	} else if (s < topRightArcStart) {
		return vec4(uLoopHalfSize.x, -straightHalfHeight + (s - rightEdgeStart), 0.0, 1.0);
	} else if (s < topEdgeStart) {
		arcCentre = vec2(straightHalfWidth, straightHalfHeight);
		startAngle = 0.0;
		along = s - topRightArcStart;
	} else if (s < topLeftArcStart) {
		return vec4(straightHalfWidth - (s - topEdgeStart), uLoopHalfSize.y, -1.0, 0.0);
	} else if (s < leftEdgeStart) {
		arcCentre = vec2(-straightHalfWidth, straightHalfHeight);
		startAngle = PI * 0.5;
		along = s - topLeftArcStart;
	} else if (s < bottomLeftArcStart) {
		return vec4(-uLoopHalfSize.x, straightHalfHeight - (s - leftEdgeStart), 0.0, -1.0);
	} else if (s < bottomLeftStraightStart) {
		arcCentre = vec2(-straightHalfWidth, -straightHalfHeight);
		startAngle = PI;
		along = s - bottomLeftArcStart;
	} else {
		return vec4(-straightHalfWidth + (s - bottomLeftStraightStart), -uLoopHalfSize.y, 1.0, 0.0);
	}
	float angle = startAngle + along / radius;
	return vec4(
		arcCentre + radius * vec2(cos(angle), sin(angle)),
		-sin(angle),
		cos(angle)
	);
}

void main() {
	// Whole pixels only: every test below runs at the block's centre.
	vec2 blockPosition = floor(vUv * uBlockCount) + 0.5;
	vec2 param = outlineParam(blockPosition - uLoopCenter);
	float arcLength = param.x;
	float across = param.y;

	if (abs(across) > uChevronArm + uChevronStroke + 1.0) discard;

	// The pattern's offset is whole blocks, so it crawls pixel by pixel.
	float shifted = arcLength - floor(uPhase);
	float cellLength = uSlot * uSlotsPerCell;
	float cellStart = floor(shifted / cellLength) * cellLength;
	float inCell = shifted - cellStart;
	float slotIndex = floor(inCell / uSlot);
	float centreSlot = floor(uSlotsPerCell * 0.5);

	bool nearChevron = abs(slotIndex - centreSlot) <= uChevronClearSlots;

	// Dashes and the chevron are both rigid sprites: each is anchored on the
	// outline and turned to the outline's heading there, so round a corner they
	// rotate but never bend. Dash rows are shifted inward so a dash's own
	// thickness is centred on the anchor.
	float dashReach = (uDashLength - 1.0) * 0.5;
	vec4 dashAnchor = pathPoint(floor(uPhase) + cellStart + slotIndex * uSlot + dashReach);
	vec2 dashTangent = normalize(dashAnchor.zw);
	vec2 dashOffset = blockPosition
		- (floor(uLoopCenter + dashAnchor.xy) + 0.5
			+ vec2(-dashTangent.y, dashTangent.x) * (uDashThickness - 1.0) * 0.5);
	bool onDash = !nearChevron
		&& abs(dot(dashOffset, dashTangent)) <= dashReach + 0.49
		&& abs(dot(dashOffset, vec2(-dashTangent.y, dashTangent.x))) <= uDashThickness * 0.5;

	// Chevron: anchored on the cell's middle slot, flipped when play runs the
	// other way so the apex always leads, and shifted inward like the dashes.
	vec4 anchor = pathPoint(floor(uPhase) + cellStart + cellLength * 0.5);
	vec2 tangent = normalize(anchor.zw);
	vec2 inward = vec2(-tangent.y, tangent.x);
	vec2 anchorBlock = floor(uLoopCenter + anchor.xy) + 0.5
		+ inward * (uDashThickness - 1.0) * 0.5;
	vec2 heading = tangent * uDirection;
	vec2 offset = blockPosition - anchorBlock;
	float forward = dot(offset, heading);
	float sideways = dot(offset, vec2(-heading.y, heading.x));
	float armLine = (uChevronArm - abs(sideways) - (forward + uChevronArm * 0.5)) * 0.70710678;
	bool onChevron = abs(sideways) <= uChevronArm + 0.5 && abs(armLine) <= uChevronStroke * 0.5;

	if (!onDash && !onChevron) discard;

	gl_FragColor = vec4(matRippleColor(vUv) * uTone, 1.0);
	#include <colorspace_fragment>
}
