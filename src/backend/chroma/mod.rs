pub mod backend;

use crate::config::HyperdeConfig;

pub fn run(configuration: &HyperdeConfig) -> Result<(), Box<dyn std::error::Error>> {
	backend::backend::run(configuration)
}