-- ----------------------------
-- Table structure for `attacks`
-- ----------------------------
DROP TABLE IF EXISTS `attacks`;
CREATE TABLE `attacks` (
  `id` INT NOT NULL AUTO_INCREMENT COMMENT 'holds the attacks unique id number' ,
  `name` VARCHAR(40) NOT NULL DEFAULT 'default' COMMENT 'is the attacks name, each name must be unique.' ,
  `image_name` VARCHAR(200) NULL DEFAULT '' COMMENT 'the icon image',
  `attack_anim` VARCHAR(40) COMMENT 'The visual effect of the attack',
  `attack_description` text COMMENT 'a short description of the attack',
  `damage` VARCHAR(40) COMMENT 'A math script to compute the chance of success and final damage. Variables defined here can be used in the range, aoe_radius, and aoe_angle MathExpressions.',
  `attackType` INT NOT NULL COMMENT 'the ID of the type of attack, found in the attack_types table',
  `delay` text COMMENT 'A MathExpression to compute the time before the attack starts (based on latency of weapon, range, attack speed, ...)',
  `range` text COMMENT 'Mostly for range attacks, melee is confined based on weapon range',
  `aoe_radius` text COMMENT 'This is the radius of the aoe attack. It can be a formula or a number; 0 for no aoe',
  `aoe_angle` text COMMENT 'The angle in front of the player the aoe attack will affect, 1-360 degrees and can also be a formula',
  `outcome` VARCHAR(40) NOT NULL COMMENT 'The effect, a progression script, of the attack',
  `requirements` VARCHAR(250) NULL DEFAULT '<pre></pre>' COMMENT 'is a xml script of requirements, these requirements will be checked before a character can use the attack. schema to be added soon, but will be very flexable.' ,
  PRIMARY KEY (`id`) ,	
  UNIQUE INDEX `name_UNIQUE` (`name`)
) COMMENT='Holds attacks';

-- The first entry (id 1) must be the default attack.
INSERT INTO `attacks` VALUES (1, 'default', '', 'hit', 'a normal attack', 'Calculate Damage', 0, 'AttackWeapon:Latency * 1000', NULL, '0', '', '', '');
INSERT INTO `attacks` VALUES (2, 'Hammer Smash', '/planeshift/materials/club01a_icon.dds', 'hit', 'an attack that causes a hammer to smash the ground and damage all nearby targets', 'Calculate Damage', 3, 'AttackWeapon:Latency * 1000', NULL, '20', '360', 'cast Hammer Smash', '');
INSERT INTO `attacks` VALUES (3, 'Hyper Shot', '/planeshift/materials/bow_higher01a_icon.dds', 'hit', 'A very powerful bow attack', 'Calculate Damage', 2, 'AttackWeapon:Latency * 1000 + Distance * 100', '60', '0', '0', 'cast Hyper Shot', '<pre> <skill name=\"ranged\" min=\"50\" max=\"4000\" /> </pre>');
INSERT INTO `attacks` VALUES (4, 'Divine Shot', '/planeshift/materials/bow_higher01a_icon.dds', 'hit', 'A long bow specific shot that takes the power of the gods and pushes it into an arrow', 'Calculate Damage', 4, 'AttackWeapon:Latency * 1000 + Distance * 100', '100', '50', '90', 'cast LB Shot', '');
